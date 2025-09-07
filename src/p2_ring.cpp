/**
 * Universidad del Valle de Guatemala
 * CC3086 Programación de Microprocesadores
 * Laboratorio 6 - Práctica 2: Búfer Circular Productor/Consumidor
 * 
 * Autor: Adrian Penagos
 * Fecha: Septiembre 2025
 * Propósito: Implementar cola FIFO thread-safe usando mutex + condition variables
 *           Evitar busy waiting y garantizar no pérdida de datos
 */

#include <pthread.h>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <cassert>
#include <unistd.h>

// ============================================================================
// CONSTANTES Y CONFIGURACIÓN
// ============================================================================

constexpr std::size_t QUEUE_SIZE = 1024;  // Tamaño del búfer circular
constexpr int POISON_PILL = -1;           // Señal de terminación

// ============================================================================
// ESTRUCTURA DEL BÚFER CIRCULAR
// ============================================================================

struct Ring {
    // Búfer circular para almacenar enteros
    int buffer[QUEUE_SIZE];
    
    // Índices del búfer circular
    std::size_t head = 0;    // Índice donde se inserta (productor)
    std::size_t tail = 0;    // Índice donde se extrae (consumidor)
    std::size_t count = 0;   // Número de elementos actuales
    
    // Primitivas de sincronización
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;   // Señal para productor
    pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;  // Señal para consumidor
    
    // Control de terminación
    bool stop_requested = false;  // Bandera de shutdown solicitado
    bool force_stop = false;      // Terminación forzada (para pruebas de timeout)
    
    // Estadísticas de monitoreo
    long total_produced = 0;
    long total_consumed = 0;
    long producer_blocks = 0;   // Cuántas veces el productor se bloqueo
    long consumer_blocks = 0;   // Cuántas veces el consumidor se bloqueo
};

// ============================================================================
// OPERACIONES DEL BÚFER CIRCULAR
// ============================================================================

/**
 * Insertar elemento en la cola (operación de productor)
 * Bloquea si la cola está llena hasta que hay espacio disponible
 * 
 * @param ring: Puntero al búfer circular
 * @param value: Valor a insertar
 * @return: true si se insertó exitosamente, false si se solicitó parada
 */
bool ring_push(Ring* ring, int value) {
    pthread_mutex_lock(&ring->mutex);
    
    // PATRÓN CRÍTICO: Usar while, no if
    // Razón: Pueden ocurrir "spurious wakeups" - despertares sin causa real
    // También maneja el caso donde múltiples hilos esperan y uno consume el espacio
    while (ring->count == QUEUE_SIZE && !ring->stop_requested && !ring->force_stop) {
        ring->producer_blocks++;
        // pthread_cond_wait libera el mutex ATÓMICAMENTE y duerme
        // Al despertar, re-adquiere el mutex automáticamente
        pthread_cond_wait(&ring->not_full, &ring->mutex);
    }
    
    // Verificar si se solicitó terminación
    if (ring->stop_requested || ring->force_stop) {
        pthread_mutex_unlock(&ring->mutex);
        return false;
    }
    
    // SECCIÓN CRÍTICA: Insertar en el búfer circular
    ring->buffer[ring->head] = value;
    ring->head = (ring->head + 1) % QUEUE_SIZE;  // Aritmética modular para circular
    ring->count++;
    ring->total_produced++;
    
    // Notificar a consumidores que hay datos disponibles
    // pthread_cond_signal despierta AL MENOS un hilo esperando
    pthread_cond_signal(&ring->not_empty);
    
    pthread_mutex_unlock(&ring->mutex);
    return true;
}

/**
 * Extraer elemento de la cola (operación de consumidor)
 * Bloquea si la cola está vacía hasta que hay datos disponibles
 * 
 * @param ring: Puntero al búfer circular
 * @param output: Puntero donde almacenar el valor extraído
 * @return: true si se extrajo exitosamente, false si cola vacía y terminando
 */
bool ring_pop(Ring* ring, int* output) {
    pthread_mutex_lock(&ring->mutex);
    
    // Esperar mientras no hay datos Y no se ha solicitado parada
    while (ring->count == 0 && !ring->stop_requested && !ring->force_stop) {
        ring->consumer_blocks++;
        pthread_cond_wait(&ring->not_empty, &ring->mutex);
    }
    
    // Si no hay datos y se solicitó parada, retornar false
    if (ring->count == 0 && (ring->stop_requested || ring->force_stop)) {
        pthread_mutex_unlock(&ring->mutex);
        return false;
    }
    
    // SECCIÓN CRÍTICA: Extraer del búfer circular
    *output = ring->buffer[ring->tail];
    ring->tail = (ring->tail + 1) % QUEUE_SIZE;
    ring->count--;
    ring->total_consumed++;
    
    // Notificar a productores que hay espacio disponible
    pthread_cond_signal(&ring->not_full);
    
    pthread_mutex_unlock(&ring->mutex);
    return true;
}

/**
 * Solicitar terminación graceful del búfer
 * Los productores y consumidores terminarán después de procesar elementos pendientes
 */
void ring_shutdown(Ring* ring) {
    pthread_mutex_lock(&ring->mutex);
    ring->stop_requested = true;
    
    // Despertar TODOS los hilos esperando (broadcast vs signal)
    // Necesario para que todos los hilos vean la bandera de stop
    pthread_cond_broadcast(&ring->not_full);
    pthread_cond_broadcast(&ring->not_empty);
    
    pthread_mutex_unlock(&ring->mutex);
}

/**
 * Forzar terminación inmediata (para pruebas de timeout)
 */
void ring_force_stop(Ring* ring) {
    pthread_mutex_lock(&ring->mutex);
    ring->force_stop = true;
    pthread_cond_broadcast(&ring->not_full);
    pthread_cond_broadcast(&ring->not_empty);
    pthread_mutex_unlock(&ring->mutex);
}

/**
 * Obtener estadísticas del búfer
 */
void ring_get_stats(Ring* ring, long* produced, long* consumed, 
                   long* prod_blocks, long* cons_blocks, std::size_t* current_size) {
    pthread_mutex_lock(&ring->mutex);
    *produced = ring->total_produced;
    *consumed = ring->total_consumed;
    *prod_blocks = ring->producer_blocks;
    *cons_blocks = ring->consumer_blocks;
    *current_size = ring->count;
    pthread_mutex_unlock(&ring->mutex);
}

// ============================================================================
// HILOS PRODUCTOR Y CONSUMIDOR
// ============================================================================

struct ProducerArgs {
    Ring* ring;
    int items_to_produce;
    int producer_id;
    int delay_us;  // Microsegundos de delay entre producciones
};

struct ConsumerArgs {
    Ring* ring;
    int consumer_id;
    int delay_us;  // Microsegundos de delay entre consumos
};

/**
 * Hilo productor: genera elementos y los inserta en la cola
 */
void* producer_thread(void* arg) {
    ProducerArgs* args = static_cast<ProducerArgs*>(arg);
    
    printf("[Productor %d] Iniciado - Producirá %d elementos\n", 
           args->producer_id, args->items_to_produce);
    
    for (int i = 0; i < args->items_to_produce; i++) {
        int value = args->producer_id * 1000000 + i;  // Valor único identificable
        
        if (!ring_push(args->ring, value)) {
            printf("[Productor %d] Terminado por shutdown en elemento %d\n", 
                   args->producer_id, i);
            break;
        }
        
        // Simular trabajo de producción
        if (args->delay_us > 0) {
            usleep(args->delay_us);
        }
    }
    
    printf("[Productor %d] Completado\n", args->producer_id);
    return nullptr;
}

/**
 * Hilo consumidor: extrae elementos de la cola y los procesa
 */
void* consumer_thread(void* arg) {
    ConsumerArgs* args = static_cast<ConsumerArgs*>(arg);
    int items_consumed = 0;
    int value;
    
    printf("[Consumidor %d] Iniciado\n", args->consumer_id);
    
    while (ring_pop(args->ring, &value)) {
        items_consumed++;
        
        // Procesar el elemento (aquí solo verificamos que no sea poison pill)
        if (value == POISON_PILL) {
            printf("[Consumidor %d] Recibió poison pill\n", args->consumer_id);
            break;
        }
        
        // Simular trabajo de procesamiento
        if (args->delay_us > 0) {
            usleep(args->delay_us);
        }
        
        // Log periódico para monitoreo
        if (items_consumed % 10000 == 0) {
            printf("[Consumidor %d] Procesados %d elementos\n", 
                   args->consumer_id, items_consumed);
        }
    }
    
    printf("[Consumidor %d] Terminado - Consumió %d elementos\n", 
           args->consumer_id, items_consumed);
    return nullptr;
}

// ============================================================================
// FUNCIÓN PRINCIPAL Y BENCHMARKS
// ============================================================================

void run_benchmark(int num_producers, int num_consumers, 
                  int items_per_producer, int test_duration_sec) {
    
    printf("\n=== BENCHMARK: %dP/%dC, %d elementos/productor ===\n", 
           num_producers, num_consumers, items_per_producer);
    
    Ring ring;
    std::vector<pthread_t> producer_threads(num_producers);
    std::vector<pthread_t> consumer_threads(num_consumers);
    std::vector<ProducerArgs> prod_args(num_producers);
    std::vector<ConsumerArgs> cons_args(num_consumers);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Crear hilos productores
    for (int i = 0; i < num_producers; i++) {
        prod_args[i] = {&ring, items_per_producer, i, 0};  // Sin delay inicial
        pthread_create(&producer_threads[i], nullptr, producer_thread, &prod_args[i]);
    }
    
    // Crear hilos consumidores
    for (int i = 0; i < num_consumers; i++) {
        cons_args[i] = {&ring, i, 0};  // Sin delay inicial
        pthread_create(&consumer_threads[i], nullptr, consumer_thread, &cons_args[i]);
    }
    
    // Esperar que terminen los productores
    for (int i = 0; i < num_producers; i++) {
        pthread_join(producer_threads[i], nullptr);
    }
    
    printf("Todos los productores terminaron\n");
    
    // Esperar un poco para que los consumidores procesen elementos pendientes
    sleep(1);
    
    // Solicitar shutdown graceful
    ring_shutdown(&ring);
    
    // Esperar que terminen los consumidores
    for (int i = 0; i < num_consumers; i++) {
        pthread_join(consumer_threads[i], nullptr);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end_time - start_time).count();
    
    // Obtener estadísticas finales
    long produced, consumed, prod_blocks, cons_blocks;
    std::size_t final_size;
    ring_get_stats(&ring, &produced, &consumed, &prod_blocks, &cons_blocks, &final_size);
    
    // Reporte de resultados
    printf("\n--- RESULTADOS ---\n");
    printf("Tiempo total: %.3f segundos\n", duration);
    printf("Elementos producidos: %ld\n", produced);
    printf("Elementos consumidos: %ld\n", consumed);
    printf("Elementos perdidos: %ld\n", produced - consumed);
    printf("Elementos finales en cola: %zu\n", final_size);
    printf("Bloqueos de productor: %ld\n", prod_blocks);
    printf("Bloqueos de consumidor: %ld\n", cons_blocks);
    printf("Throughput producción: %.2f items/seg\n", produced / duration);
    printf("Throughput consumo: %.2f items/seg\n", consumed / duration);
    
    // Limpiar recursos
    pthread_mutex_destroy(&ring.mutex);
    pthread_cond_destroy(&ring.not_full);
    pthread_cond_destroy(&ring.not_empty);
}

int main(int argc, char** argv) {
    printf("=== LABORATORIO 6 - PRÁCTICA 2: PRODUCTOR-CONSUMIDOR ===\n");
    
    // Parámetros configurables
    int num_producers = (argc > 1) ? std::atoi(argv[1]) : 2;
    int num_consumers = (argc > 2) ? std::atoi(argv[2]) : 2;
    int items_per_producer = (argc > 3) ? std::atoi(argv[3]) : 100000;
    int test_duration = (argc > 4) ? std::atoi(argv[4]) : 10;
    
    printf("Configuración por defecto: %dP/%dC\n", num_producers, num_consumers);
    printf("Tamaño de cola: %zu elementos\n", QUEUE_SIZE);
    
    // Ejecutar diferentes configuraciones de benchmark
    run_benchmark(1, 1, items_per_producer, test_duration);  // SPSC (Single Producer Single Consumer)
    run_benchmark(2, 1, items_per_producer, test_duration);  // MPSC (Multi Producer Single Consumer)
    run_benchmark(1, 2, items_per_producer, test_duration);  // SPMC (Single Producer Multi Consumer)
    run_benchmark(num_producers, num_consumers, items_per_producer, test_duration);  // MPMC
    
    printf("\n=== ANÁLISIS ===\n");
    printf("• SPSC: Máxima eficiencia, mínima contención\n");
    printf("• MPSC: Contención en producción, consumo serial\n");
    printf("• SPMC: Producción serial, contención en consumo\n");
    printf("• MPMC: Máxima contención, pero máximo paralelismo\n");
    
    printf("\n=== PREGUNTAS GUÍA RESPONDIDAS ===\n");
    printf("• ¿Por qué while y no if? → Spurious wakeups y múltiples hilos\n");
    printf("• ¿Shutdown limpio? → Bandera + broadcast para despertar todos\n");
    printf("• ¿Signal vs broadcast? → Signal para eficiencia, broadcast para shutdown\n");
    
    return 0;
}