/**
 * Universidad del Valle de Guatemala
 * CC3086 Programación de Microprocesadores
 * Laboratorio 6 - Práctica 5: Pipeline con Barreras y pthread_once
 * 
 * Autor: Adrian Penagos
 * Fecha: Septiembre 2025
 * Propósito: Construir pipeline de 3 etapas sincronizado con barriers
 *           Inicialización única con pthread_once y medición de throughput
 */

#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <queue>
#include <fstream>
#include <cassert>
#include <unistd.h>
#include <random>
#include <cstring>
#include <cmath>  

// ============================================================================
// CONSTANTES Y CONFIGURACIÓN DEL PIPELINE
// ============================================================================

constexpr int DEFAULT_TICKS = 1000;        // Número de iteraciones del pipeline
constexpr int BUFFER_SIZE = 100;           // Tamaño de búfers entre etapas
constexpr int DATA_RANGE = 10000;          // Rango de datos a procesar

// Tipos de datos que fluyen por el pipeline
struct DataItem {
    int id;                    // Identificador único
    int raw_value;            // Valor original (etapa 1)
    double processed_value;   // Valor procesado (etapa 2)  
    bool is_valid;            // Resultado de filtrado (etapa 3)
    std::chrono::steady_clock::time_point timestamp;  // Para medir latencia
    
    DataItem() : id(-1), raw_value(0), processed_value(0.0), is_valid(false) {}
    DataItem(int _id, int _val) : id(_id), raw_value(_val), processed_value(0.0), 
                                  is_valid(false), timestamp(std::chrono::steady_clock::now()) {}
};

// ============================================================================
// RECURSOS COMPARTIDOS Y SINCRONIZACIÓN
// ============================================================================

// Barrier para sincronización entre etapas
static pthread_barrier_t pipeline_barrier;

// pthread_once para inicialización única
static pthread_once_t once_flag = PTHREAD_ONCE_INIT;

// Búfers entre etapas (protegidos por mutex)
static std::queue<DataItem> stage1_to_stage2;
static std::queue<DataItem> stage2_to_stage3;
static pthread_mutex_t buffer1_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t buffer2_mutex = PTHREAD_MUTEX_INITIALIZER;

// Condición para señalizar datos disponibles/espacio disponible
static pthread_cond_t buffer1_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t buffer1_not_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t buffer2_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t buffer2_not_full = PTHREAD_COND_INITIALIZER;

// Control de terminación del pipeline
static bool pipeline_shutdown = false;
static pthread_mutex_t shutdown_mutex = PTHREAD_MUTEX_INITIALIZER;

// Recursos compartidos globales (inicializados una sola vez)
static std::ofstream* log_file = nullptr;
static std::mt19937* global_rng = nullptr;
static double* lookup_table = nullptr;

// Estadísticas globales del pipeline
struct PipelineStats {
    long items_generated = 0;
    long items_processed = 0;
    long items_filtered = 0;
    long barrier_waits = 0;
    double total_latency_ms = 0.0;
    pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    void add_latency(double latency_ms) {
        pthread_mutex_lock(&stats_mutex);
        total_latency_ms += latency_ms;
        pthread_mutex_unlock(&stats_mutex);
    }
    
    void print_final_stats() {
        pthread_mutex_lock(&stats_mutex);
        printf("\n=== ESTADÍSTICAS FINALES DEL PIPELINE ===\n");
        printf("Items generados:   %ld\n", items_generated);
        printf("Items procesados:  %ld\n", items_processed);
        printf("Items filtrados:   %ld (%.1f%%)\n", items_filtered, 
               100.0 * items_filtered / items_generated);
        printf("Esperas en barrier: %ld\n", barrier_waits);
        printf("Latencia promedio: %.2f ms\n", 
               items_filtered > 0 ? total_latency_ms / items_filtered : 0.0);
        pthread_mutex_unlock(&stats_mutex);
    }
} pipeline_stats;

// ============================================================================
// INICIALIZACIÓN ÚNICA CON PTHREAD_ONCE
// ============================================================================

/**
 * Función de inicialización ejecutada UNA SOLA VEZ
 * pthread_once garantiza que solo un hilo ejecute esta función
 */
static void init_shared_resources() {
    printf("🔧 Inicializando recursos compartidos (pthread_once)...\n");
    
    // Abrir archivo de log para el pipeline
    log_file = new std::ofstream("data/pipeline_log.txt");
    if (log_file->is_open()) {
        *log_file << "Pipeline Log - Timestamp: " 
                  << std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count()
                  << std::endl;
        *log_file << "Stage,ItemID,Value,Timestamp" << std::endl;
        printf("✅ Archivo de log abierto exitosamente\n");
    } else {
        printf("❌ Error abriendo archivo de log\n");
    }
    
    // Inicializar generador de números aleatorios global
    global_rng = new std::mt19937(42);  // Semilla fija para reproducibilidad
    printf("✅ Generador RNG inicializado\n");
    
    // Crear tabla de lookup para optimizar cálculos en etapa 2
    lookup_table = new double[DATA_RANGE];
    for (int i = 0; i < DATA_RANGE; i++) {
        // Función costosa de calcular: sqrt(sin(x)^2 + cos(x)^2) = 1, pero simulamos cómputo
        lookup_table[i] = sqrt(sin(i * 0.001) * sin(i * 0.001) + 
                             cos(i * 0.001) * cos(i * 0.001));
    }
    printf("✅ Tabla de lookup precalculada (%d entradas)\n", DATA_RANGE);
    
    printf("🎯 Inicialización única completada exitosamente\n");
}

// ============================================================================
// FUNCIONES AUXILIARES PARA BÚFERS
// ============================================================================

/**
 * Insertar item en búfer entre etapas 1 y 2
 */
bool push_to_buffer1(const DataItem& item, int timeout_ms = 1000) {
    struct timespec abs_timeout;
    clock_gettime(CLOCK_REALTIME, &abs_timeout);
    abs_timeout.tv_sec += timeout_ms / 1000;
    abs_timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (abs_timeout.tv_nsec >= 1000000000) {
        abs_timeout.tv_sec++;
        abs_timeout.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&buffer1_mutex);
    
    // Esperar espacio disponible
    while (stage1_to_stage2.size() >= BUFFER_SIZE && !pipeline_shutdown) {
        if (pthread_cond_timedwait(&buffer1_not_full, &buffer1_mutex, &abs_timeout) != 0) {
            pthread_mutex_unlock(&buffer1_mutex);
            return false;  // Timeout
        }
    }
    
    if (pipeline_shutdown) {
        pthread_mutex_unlock(&buffer1_mutex);
        return false;
    }
    
    stage1_to_stage2.push(item);
    pthread_cond_signal(&buffer1_not_empty);
    pthread_mutex_unlock(&buffer1_mutex);
    
    // Log de la operación
    if (log_file && log_file->is_open()) {
        *log_file << "Stage1," << item.id << "," << item.raw_value << "," 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch()).count() 
                  << std::endl;
    }
    
    return true;
}

/**
 * Extraer item del búfer entre etapas 1 y 2
 */
bool pop_from_buffer1(DataItem& item, int timeout_ms = 1000) {
    struct timespec abs_timeout;
    clock_gettime(CLOCK_REALTIME, &abs_timeout);
    abs_timeout.tv_sec += timeout_ms / 1000;
    abs_timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (abs_timeout.tv_nsec >= 1000000000) {
        abs_timeout.tv_sec++;
        abs_timeout.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&buffer1_mutex);
    
    while (stage1_to_stage2.empty() && !pipeline_shutdown) {
        if (pthread_cond_timedwait(&buffer1_not_empty, &buffer1_mutex, &abs_timeout) != 0) {
            pthread_mutex_unlock(&buffer1_mutex);
            return false;  // Timeout
        }
    }
    
    if (stage1_to_stage2.empty()) {
        pthread_mutex_unlock(&buffer1_mutex);
        return false;
    }
    
    item = stage1_to_stage2.front();
    stage1_to_stage2.pop();
    pthread_cond_signal(&buffer1_not_full);
    pthread_mutex_unlock(&buffer1_mutex);
    
    return true;
}

/**
 * Insertar item en búfer entre etapas 2 y 3
 */
bool push_to_buffer2(const DataItem& item, int timeout_ms = 1000) {
    struct timespec abs_timeout;
    clock_gettime(CLOCK_REALTIME, &abs_timeout);
    abs_timeout.tv_sec += timeout_ms / 1000;
    abs_timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (abs_timeout.tv_nsec >= 1000000000) {
        abs_timeout.tv_sec++;
        abs_timeout.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&buffer2_mutex);
    
    while (stage2_to_stage3.size() >= BUFFER_SIZE && !pipeline_shutdown) {
        if (pthread_cond_timedwait(&buffer2_not_full, &buffer2_mutex, &abs_timeout) != 0) {
            pthread_mutex_unlock(&buffer2_mutex);
            return false;  // Timeout
        }
    }
    
    if (pipeline_shutdown) {
        pthread_mutex_unlock(&buffer2_mutex);
        return false;
    }
    
    stage2_to_stage3.push(item);
    pthread_cond_signal(&buffer2_not_empty);
    pthread_mutex_unlock(&buffer2_mutex);
    
    if (log_file && log_file->is_open()) {
        *log_file << "Stage2," << item.id << "," << item.processed_value << "," 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch()).count() 
                  << std::endl;
    }
    
    return true;
}

/**
 * Extraer item del búfer entre etapas 2 y 3
 */
bool pop_from_buffer2(DataItem& item, int timeout_ms = 1000) {
    struct timespec abs_timeout;
    clock_gettime(CLOCK_REALTIME, &abs_timeout);
    abs_timeout.tv_sec += timeout_ms / 1000;
    abs_timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (abs_timeout.tv_nsec >= 1000000000) {
        abs_timeout.tv_sec++;
        abs_timeout.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&buffer2_mutex);
    
    while (stage2_to_stage3.empty() && !pipeline_shutdown) {
        if (pthread_cond_timedwait(&buffer2_not_empty, &buffer2_mutex, &abs_timeout) != 0) {
            pthread_mutex_unlock(&buffer2_mutex);
            return false;  // Timeout
        }
    }
    
    if (stage2_to_stage3.empty()) {
        pthread_mutex_unlock(&buffer2_mutex);
        return false;
    }
    
    item = stage2_to_stage3.front();
    stage2_to_stage3.pop();
    pthread_cond_signal(&buffer2_not_full);
    pthread_mutex_unlock(&buffer2_mutex);
    
    return true;
}

// ============================================================================
// ETAPAS DEL PIPELINE
// ============================================================================

/**
 * ETAPA 1: GENERADOR
 * Genera datos de entrada para el pipeline
 */
void* stage_generator(void* arg) {
    long stage_id = reinterpret_cast<long>(arg);
    int ticks = DEFAULT_TICKS;
    
    printf("[Etapa %ld - Generador] Iniciado\n", stage_id);
    
    // Ejecutar inicialización única
    pthread_once(&once_flag, init_shared_resources);
    
    std::uniform_int_distribution<int> value_dist(1, DATA_RANGE);
    
    for (int tick = 0; tick < ticks; tick++) {
        // Generar nuevo item de datos
        int raw_value = value_dist(*global_rng);
        DataItem item(tick, raw_value);
        
        // Intentar insertar en búfer hacia etapa 2
        if (!push_to_buffer1(item)) {
            printf("[Etapa %ld] Timeout/Error enviando item %d\n", stage_id, tick);
            break;
        }
        
        // Actualizar estadísticas
        pthread_mutex_lock(&pipeline_stats.stats_mutex);
        pipeline_stats.items_generated++;
        pthread_mutex_unlock(&pipeline_stats.stats_mutex);
        
        // Log periódico
        if (tick % 100 == 0) {
            printf("[Etapa %ld] Generados %d items\n", stage_id, tick + 1);
        }
        
        // Sincronización con barrier
        pthread_mutex_lock(&pipeline_stats.stats_mutex);
        pipeline_stats.barrier_waits++;
        pthread_mutex_unlock(&pipeline_stats.stats_mutex);
        
        int barrier_result = pthread_barrier_wait(&pipeline_barrier);
        if (barrier_result != 0 && barrier_result != PTHREAD_BARRIER_SERIAL_THREAD) {
            printf("[Etapa %ld] Error en barrier: %d\n", stage_id, barrier_result);
        }
        
        // Pequeña pausa para simular trabajo de generación
        usleep(1000);  // 1ms
    }
    
    printf("[Etapa %ld - Generador] Completado - %d items generados\n", stage_id, ticks);
    return nullptr;
}

/**
 * ETAPA 2: PROCESADOR
 * Aplica transformaciones complejas a los datos
 */
void* stage_processor(void* arg) {
    long stage_id = reinterpret_cast<long>(arg);
    int processed_count = 0;
    
    printf("[Etapa %ld - Procesador] Iniciado\n", stage_id);
    
    // Ejecutar inicialización única
    pthread_once(&once_flag, init_shared_resources);
    
    for (int tick = 0; tick < DEFAULT_TICKS; tick++) {
        DataItem item;
        
        // Obtener item del búfer de entrada
        if (!pop_from_buffer1(item)) {
            printf("[Etapa %ld] Timeout obteniendo item en tick %d\n", stage_id, tick);
            break;
        }
        
        // PROCESAMIENTO INTENSIVO:
        // 1. Aplicar función compleja usando tabla de lookup
        double base_value = lookup_table[item.raw_value % DATA_RANGE];
        
        // 2. Aplicar transformaciones matemáticas
        item.processed_value = base_value * log(item.raw_value + 1);
        item.processed_value += sin(item.raw_value * 0.01) * cos(item.id * 0.02);
        
        // 3. Normalización
        item.processed_value = fabs(item.processed_value);
        
        // Simular cómputo intensivo
        volatile double temp = 0.0;
        for (int i = 0; i < 1000; i++) {
            temp += sqrt(i + item.raw_value);
        }
        item.processed_value += temp * 1e-6;  // Agregar resultado para evitar optimización
        
        processed_count++;
        
        // Enviar a la siguiente etapa
        if (!push_to_buffer2(item)) {
            printf("[Etapa %ld] Error enviando item procesado %d\n", stage_id, item.id);
            break;
        }
        
        // Actualizar estadísticas
        pthread_mutex_lock(&pipeline_stats.stats_mutex);
        pipeline_stats.items_processed++;
        pthread_mutex_unlock(&pipeline_stats.stats_mutex);
        
        if (processed_count % 100 == 0) {
            printf("[Etapa %ld] Procesados %d items\n", stage_id, processed_count);
        }
        
        // Sincronización con barrier
        pthread_mutex_lock(&pipeline_stats.stats_mutex);
        pipeline_stats.barrier_waits++;
        pthread_mutex_unlock(&pipeline_stats.stats_mutex);
        
        pthread_barrier_wait(&pipeline_barrier);
    }
    
    printf("[Etapa %ld - Procesador] Completado - %d items procesados\n", 
           stage_id, processed_count);
    return nullptr;
}

/**
 * ETAPA 3: FILTRO Y REDUCTOR
 * Filtra datos válidos y los agrega a resultado final
 */
void* stage_filter_reduce(void* arg) {
    long stage_id = reinterpret_cast<long>(arg);
    int filtered_count = 0;
    double accumulated_result = 0.0;
    
    printf("[Etapa %ld - Filtro/Reduce] Iniciado\n", stage_id);
    
    // Ejecutar inicialización única
    pthread_once(&once_flag, init_shared_resources);
    
    for (int tick = 0; tick < DEFAULT_TICKS; tick++) {
        DataItem item;
        
        // Obtener item del búfer de entrada
        if (!pop_from_buffer2(item)) {
            printf("[Etapa %ld] Timeout obteniendo item en tick %d\n", stage_id, tick);
            break;
        }
        
        // FILTRADO: Solo aceptar items que cumplan criterios
        bool passes_filter = false;
        
        // Criterio 1: Valor procesado dentro de rango válido
        if (item.processed_value > 0.1 && item.processed_value < 100.0) {
            // Criterio 2: ID no divisible por 13 (superstición del pipeline)
            if (item.id % 13 != 0) {
                // Criterio 3: Valor original cumple algún patrón
                if ((item.raw_value % 3 == 0) || (item.raw_value % 7 == 0)) {
                    passes_filter = true;
                }
            }
        }
        
        if (passes_filter) {
            item.is_valid = true;
            filtered_count++;
            
            // REDUCCIÓN: Agregar al resultado acumulado
            accumulated_result += item.processed_value;
            
            // Calcular latencia end-to-end
            auto now = std::chrono::steady_clock::now();
            double latency_ms = std::chrono::duration<double, std::milli>(
                now - item.timestamp).count();
            
            pipeline_stats.add_latency(latency_ms);
            
            // Log del item filtrado
            if (log_file && log_file->is_open()) {
                *log_file << "Stage3," << item.id << "," << item.processed_value << "," 
                          << std::chrono::duration_cast<std::chrono::milliseconds>(
                               now.time_since_epoch()).count() 
                          << " (VALID, latency=" << latency_ms << "ms)" << std::endl;
            }
        } else {
            item.is_valid = false;
        }
        
        // Actualizar estadísticas
        if (item.is_valid) {
            pthread_mutex_lock(&pipeline_stats.stats_mutex);
            pipeline_stats.items_filtered++;
            pthread_mutex_unlock(&pipeline_stats.stats_mutex);
        }
        
        if ((tick + 1) % 100 == 0) {
            printf("[Etapa %ld] Procesados %d items, %d válidos (%.1f%%), suma=%.2f\n", 
                   stage_id, tick + 1, filtered_count, 
                   100.0 * filtered_count / (tick + 1), accumulated_result);
        }
        
        // Sincronización con barrier
        pthread_mutex_lock(&pipeline_stats.stats_mutex);
        pipeline_stats.barrier_waits++;
        pthread_mutex_unlock(&pipeline_stats.stats_mutex);
        
        pthread_barrier_wait(&pipeline_barrier);
    }
    
    printf("[Etapa %ld - Filtro/Reduce] Completado\n", stage_id);
    printf("  Items válidos: %d\n", filtered_count);
    printf("  Resultado acumulado: %.6f\n", accumulated_result);
    
    return nullptr;
}

// ============================================================================
// CONTROL DE SHUTDOWN GRACEFUL
// ============================================================================

void request_pipeline_shutdown() {
    pthread_mutex_lock(&shutdown_mutex);
    pipeline_shutdown = true;
    pthread_mutex_unlock(&shutdown_mutex);
    
    // Despertar todos los hilos esperando en condiciones
    pthread_cond_broadcast(&buffer1_not_empty);
    pthread_cond_broadcast(&buffer1_not_full);
    pthread_cond_broadcast(&buffer2_not_empty);
    pthread_cond_broadcast(&buffer2_not_full);
    
    printf("🛑 Shutdown del pipeline solicitado\n");
}

// ============================================================================
// FUNCIÓN PRINCIPAL Y BENCHMARKS
// ============================================================================

void run_pipeline_benchmark(int num_ticks) {
    printf("============================================================\n");
    printf("🏭 EJECUTANDO PIPELINE BENCHMARK\n");
    printf("============================================================\n");
    printf("Configuración: %d ticks por etapa\n", num_ticks);
    
    // Reinicializar barrier para 3 etapas
    pthread_barrier_init(&pipeline_barrier, nullptr, 3);
    
    // Reset de variables globales
    pipeline_shutdown = false;
    pipeline_stats = PipelineStats{};
    
    // Crear hilos para cada etapa del pipeline
    pthread_t generator_thread, processor_thread, filter_thread;
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Crear etapas del pipeline
    pthread_create(&generator_thread, nullptr, stage_generator, reinterpret_cast<void*>(1));
    pthread_create(&processor_thread, nullptr, stage_processor, reinterpret_cast<void*>(2));
    pthread_create(&filter_thread, nullptr, stage_filter_reduce, reinterpret_cast<void*>(3));
    
    printf("🚀 Pipeline iniciado con 3 etapas\n");
    
    // Esperar terminación de todas las etapas
    pthread_join(generator_thread, nullptr);
    printf("✅ Etapa generadora terminada\n");
    
    pthread_join(processor_thread, nullptr);
    printf("✅ Etapa procesadora terminada\n");
    
    pthread_join(filter_thread, nullptr);
    printf("✅ Etapa filtro/reduce terminada\n");
    
    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration<double>(end_time - start_time).count();
    
    printf("\n⏱️  RESULTADOS DEL BENCHMARK\n");
    printf("Tiempo total de ejecución: %.3f segundos\n", total_duration);
    printf("Throughput del pipeline: %.2f items/seg\n", 
           pipeline_stats.items_generated / total_duration);
    printf("Eficiencia de filtrado: %.1f%%\n", 
           100.0 * pipeline_stats.items_filtered / pipeline_stats.items_generated);
    
    // Mostrar estadísticas detalladas
    pipeline_stats.print_final_stats();
    
    // Verificar balance del pipeline
    printf("\n📊 ANÁLISIS DE BALANCE:\n");
    if (pipeline_stats.items_generated > pipeline_stats.items_processed + 50) {
        printf("⚠️  Etapa procesadora es cuello de botella\n");
    } else if (pipeline_stats.items_processed > pipeline_stats.items_filtered + 50) {
        printf("⚠️  Etapa filtro es cuello de botella\n");
    } else {
        printf("✅ Pipeline bien balanceado\n");
    }
    
    // Cleanup del barrier
    pthread_barrier_destroy(&pipeline_barrier);
}

int main(int argc, char** argv) {
    printf("=== LABORATORIO 6 - PRÁCTICA 5: PIPELINE CON BARRERAS ===\n");
    
    int num_ticks = (argc > 1) ? std::atoi(argv[1]) : DEFAULT_TICKS;
    printf("Configuración: %d ticks por etapa\n", num_ticks);
    
    // Crear directorio de datos si no existe
    system("mkdir -p data");
    
    // Ejecutar benchmark del pipeline
    run_pipeline_benchmark(num_ticks);
    
    printf("============================================================\n");
    printf("=== ANÁLISIS DE DISEÑO ===\n");
    printf("============================================================\n");
    
    printf("🔄 BARRERAS vs COLAS:\n");
    printf("• Barreras: Sincronización por lotes (batch processing)\n");
    printf("  + Garantiza procesamiento en lockstep\n");
    printf("  + Fácil de debuggear y medir\n");
    printf("  - Menor throughput por esperas\n");
    printf("  - El más lento determina la velocidad total\n\n");
    
    printf("• Colas: Procesamiento continuo (streaming)\n");
    printf("  + Mayor throughput al evitar esperas\n");
    printf("  + Mejor utilización de recursos\n");
    printf("  - Más complejo de sincronizar\n");
    printf("  - Posibles desbalances entre etapas\n\n");
    
    printf("⚡ MEDICIÓN DE THROUGHPUT POR ETAPA:\n");
    printf("• Usar timestamps en DataItem para medir latencias\n");
    printf("• Contadores atómicos para operaciones completadas\n");
    printf("• Muestreo periódico para detectar cuellos de botella\n\n");
    
    printf("🛑 GRACEFUL SHUTDOWN:\n");
    printf("• Bandera global de shutdown\n");
    printf("• Broadcast a todas las condition variables\n");
    printf("• Timeout en operaciones de buffer\n");
    printf("• Join de todos los hilos antes de limpiar recursos\n\n");
    
    printf("🔧 PTHREAD_ONCE:\n");
    printf("• Garantiza inicialización única de recursos costosos\n");
    printf("• Thread-safe sin overhead de mutex en llamadas subsecuentes\n");
    printf("• Útil para: abrir archivos, precomputar tablas, init RNG\n\n");
    
    printf("=== PREGUNTAS GUÍA RESPONDIDAS ===\n");
    printf("• ¿Dónde conviene barrera vs colas?\n");
    printf("  → Barreras para debugging y análisis, colas para producción\n");
    printf("• ¿Cómo medir throughput por etapa?\n");
    printf("  → Timestamps + contadores atómicos + sampling periódico\n");
    printf("• ¿Cómo graceful shutdown sin deadlocks?\n");
    printf("  → Bandera global + timeouts + broadcast + join ordenado\n");
    
    // Cleanup de recursos globales
    if (log_file) {
        log_file->close();
        delete log_file;
    }
    if (global_rng) {
        delete global_rng;
    }
    if (lookup_table) {
        delete[] lookup_table;
    }
    
    // Cleanup de primitivas de sincronización
    pthread_mutex_destroy(&buffer1_mutex);
    pthread_mutex_destroy(&buffer2_mutex);
    pthread_mutex_destroy(&shutdown_mutex);
    pthread_mutex_destroy(&pipeline_stats.stats_mutex);
    pthread_cond_destroy(&buffer1_not_empty);
    pthread_cond_destroy(&buffer1_not_full);
    pthread_cond_destroy(&buffer2_not_empty);
    pthread_cond_destroy(&buffer2_not_full);
    
    printf("\n✅ Programa terminado exitosamente\n");
    printf("📄 Log generado en: data/pipeline_log.txt\n");
    
    return 0;
}