/**
 * Universidad del Valle de Guatemala
 * CC3086 Programación de Microprocesadores
 * Laboratorio 6 - Práctica 4: Deadlock Intencional, Diagnóstico y Corrección
 * 
 * Autor: Adrian Penagos
 * Fecha: Septiembre 2025
 * Propósito: Reproducir interbloqueo con dos mutex, diagnosticar y corregir
 *           usando orden total y trylock con backoff
 */

#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <cassert>
#include <random>

// ============================================================================
// RECURSOS COMPARTIDOS Y SINCRONIZACIÓN
// ============================================================================

pthread_mutex_t mutex_A = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_B = PTHREAD_MUTEX_INITIALIZER;

// Variables compartidas protegidas por los mutex
int shared_resource_A = 0;
int shared_resource_B = 0;

// Estadísticas globales
struct GlobalStats {
    long successful_operations = 0;
    long deadlock_attempts = 0;
    long backoff_retries = 0;
    long timeouts = 0;
    pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    void increment_success() {
        pthread_mutex_lock(&stats_mutex);
        successful_operations++;
        pthread_mutex_unlock(&stats_mutex);
    }
    
    void increment_deadlock() {
        pthread_mutex_lock(&stats_mutex);
        deadlock_attempts++;
        pthread_mutex_unlock(&stats_mutex);
    }
    
    void increment_backoff() {
        pthread_mutex_lock(&stats_mutex);
        backoff_retries++;
        pthread_mutex_unlock(&stats_mutex);
    }
    
    void increment_timeout() {
        pthread_mutex_lock(&stats_mutex);
        timeouts++;
        pthread_mutex_unlock(&stats_mutex);
    }
    
    void print_stats() {
        pthread_mutex_lock(&stats_mutex);
        printf("=== ESTADÍSTICAS GLOBALES ===\n");
        printf("Operaciones exitosas: %ld\n", successful_operations);
        printf("Intentos de deadlock: %ld\n", deadlock_attempts);
        printf("Reintentos por backoff: %ld\n", backoff_retries);
        printf("Timeouts: %ld\n", timeouts);
        pthread_mutex_unlock(&stats_mutex);
    }
} global_stats;

// ============================================================================
// VERSIÓN 1: DEADLOCK INTENCIONAL
// ============================================================================

/**
 * Hilo 1: Adquiere mutex A, luego mutex B
 * Esta estrategia puede causar deadlock con el hilo 2
 */
void* thread_deadlock_1(void* arg) {
    int thread_id = *static_cast<int*>(arg);
    printf("[Hilo %d] Iniciado - Estrategia: A -> B\n", thread_id);
    
    for (int i = 0; i < 5; i++) {
        printf("[Hilo %d] Iteración %d: Intentando adquirir mutex A\n", thread_id, i);
        pthread_mutex_lock(&mutex_A);
        printf("[Hilo %d] ✅ Mutex A adquirido\n", thread_id);
        
        // Simular trabajo que requiere solo recurso A
        shared_resource_A += 10;
        
        // ⚠️ PUNTO CRÍTICO: Pequeña pausa aumenta probabilidad de deadlock
        usleep(100000);  // 100ms - tiempo suficiente para que otro hilo tome mutex B
        
        printf("[Hilo %d] Intentando adquirir mutex B...\n", thread_id);
        // 🔒 AQUÍ PUEDE OCURRIR EL DEADLOCK
        pthread_mutex_lock(&mutex_B);
        printf("[Hilo %d] ✅ Mutex B adquirido\n", thread_id);
        
        // Trabajo que requiere ambos recursos
        shared_resource_B += shared_resource_A;
        printf("[Hilo %d] Operación completada: A=%d, B=%d\n", 
               thread_id, shared_resource_A, shared_resource_B);
        
        // Liberar en orden inverso (LIFO - Last In First Out)
        pthread_mutex_unlock(&mutex_B);
        printf("[Hilo %d] Mutex B liberado\n", thread_id);
        pthread_mutex_unlock(&mutex_A);
        printf("[Hilo %d] Mutex A liberado\n", thread_id);
        
        global_stats.increment_success();
        usleep(50000);  // Pausa entre iteraciones
    }
    
    printf("[Hilo %d] Terminado\n", thread_id);
    return nullptr;
}

/**
 * Hilo 2: Adquiere mutex B, luego mutex A
 * Orden OPUESTO al hilo 1 - causa deadlock
 */
void* thread_deadlock_2(void* arg) {
    int thread_id = *static_cast<int*>(arg);
    printf("[Hilo %d] Iniciado - Estrategia: B -> A\n", thread_id);
    
    for (int i = 0; i < 5; i++) {
        printf("[Hilo %d] Iteración %d: Intentando adquirir mutex B\n", thread_id, i);
        pthread_mutex_lock(&mutex_B);
        printf("[Hilo %d] ✅ Mutex B adquirido\n", thread_id);
        
        // Simular trabajo que requiere solo recurso B
        shared_resource_B += 5;
        
        // ⚠️ PUNTO CRÍTICO: Pausa para aumentar probabilidad de deadlock
        usleep(100000);  // 100ms
        
        printf("[Hilo %d] Intentando adquirir mutex A...\n", thread_id);
        // 🔒 AQUÍ PUEDE OCURRIR EL DEADLOCK
        pthread_mutex_lock(&mutex_A);
        printf("[Hilo %d] ✅ Mutex A adquirido\n", thread_id);
        
        // Trabajo que requiere ambos recursos
        shared_resource_A += shared_resource_B;
        printf("[Hilo %d] Operación completada: A=%d, B=%d\n", 
               thread_id, shared_resource_A, shared_resource_B);
        
        // Liberar en orden inverso
        pthread_mutex_unlock(&mutex_A);
        printf("[Hilo %d] Mutex A liberado\n", thread_id);
        pthread_mutex_unlock(&mutex_B);
        printf("[Hilo %d] Mutex B liberado\n", thread_id);
        
        global_stats.increment_success();
        usleep(50000);  // Pausa entre iteraciones
    }
    
    printf("[Hilo %d] Terminado\n", thread_id);
    return nullptr;
}

// ============================================================================
// VERSIÓN 2: CORRECCIÓN CON ORDEN TOTAL
// ============================================================================

/**
 * Estrategia de corrección: ORDEN TOTAL de mutex
 * Todos los hilos adquieren mutex en el mismo orden: A -> B
 * Esto previene ciclos en el grafo de espera
 */
void* thread_ordered_lock(void* arg) {
    int thread_id = *static_cast<int*>(arg);
    printf("[Hilo %d] Iniciado - Estrategia: Orden total A -> B\n", thread_id);
    
    for (int i = 0; i < 10; i++) {
        // ORDEN FIJO: Siempre A primero, luego B
        printf("[Hilo %d] Iter %d: Adquiriendo mutex A\n", thread_id, i);
        pthread_mutex_lock(&mutex_A);
        
        printf("[Hilo %d] Adquiriendo mutex B\n", thread_id);
        pthread_mutex_lock(&mutex_B);
        
        // Trabajo crítico con ambos recursos
        shared_resource_A += thread_id;
        shared_resource_B += shared_resource_A;
        
        // Simular trabajo computacional
        usleep(10000);  // 10ms
        
        // Liberar en orden inverso (buena práctica)
        pthread_mutex_unlock(&mutex_B);
        pthread_mutex_unlock(&mutex_A);
        
        global_stats.increment_success();
        usleep(5000);
    }
    
    printf("[Hilo %d] Terminado exitosamente\n", thread_id);
    return nullptr;
}

// ============================================================================
// VERSIÓN 3: CORRECCIÓN CON TRYLOCK Y BACKOFF
// ============================================================================

/**
 * Estrategia de corrección: TRYLOCK con exponential backoff
 * Si no puede adquirir el segundo mutex, libera el primero y reintenta
 */
void* thread_trylock_backoff(void* arg) {
    int thread_id = *static_cast<int*>(arg);
    bool prefer_A_first = (thread_id % 2 == 0);  // Hilos pares prefieren A->B
    
    printf("[Hilo %d] Iniciado - Estrategia: Trylock con backoff (%s)\n", 
           thread_id, prefer_A_first ? "A->B" : "B->A");
    
    std::mt19937 rng(thread_id);  // RNG con semilla única por hilo
    std::uniform_int_distribution<int> backoff_dist(1000, 10000);  // 1-10ms
    
    for (int i = 0; i < 10; i++) {
        bool operation_complete = false;
        int retry_count = 0;
        int backoff_time = 1000;  // Empezar con 1ms
        
        while (!operation_complete && retry_count < 50) {  // Máximo 50 intentos
            pthread_mutex_t* first_mutex = prefer_A_first ? &mutex_A : &mutex_B;
            pthread_mutex_t* second_mutex = prefer_A_first ? &mutex_B : &mutex_A;
            
            // Adquirir primer mutex (bloqueo normal)
            pthread_mutex_lock(first_mutex);
            
            // Intentar adquirir segundo mutex (no bloqueante)
            if (pthread_mutex_trylock(second_mutex) == 0) {
                // ✅ Éxito: ambos mutex adquiridos
                
                // Trabajo crítico
                if (prefer_A_first) {
                    shared_resource_A += thread_id;
                    shared_resource_B += shared_resource_A;
                } else {
                    shared_resource_B += thread_id;
                    shared_resource_A += shared_resource_B;
                }
                
                usleep(5000);  // Simular trabajo
                
                // Liberar ambos mutex
                pthread_mutex_unlock(second_mutex);
                pthread_mutex_unlock(first_mutex);
                
                operation_complete = true;
                global_stats.increment_success();
                
                if (retry_count > 0) {
                    printf("[Hilo %d] Iter %d: Éxito después de %d reintentos\n", 
                           thread_id, i, retry_count);
                }
                
            } else {
                // ❌ Fallo: no se pudo adquirir segundo mutex
                pthread_mutex_unlock(first_mutex);  // Liberar primer mutex
                
                retry_count++;
                global_stats.increment_backoff();
                
                // Exponential backoff con jitter
                int jitter = backoff_dist(rng);
                usleep(backoff_time + jitter);
                
                // Duplicar tiempo de backoff (hasta un máximo)
                backoff_time = std::min(backoff_time * 2, 50000);
                
                if (retry_count % 10 == 0) {
                    printf("[Hilo %d] Iter %d: %d reintentos...\n", 
                           thread_id, i, retry_count);
                }
            }
        }
        
        if (!operation_complete) {
            printf("[Hilo %d] ⚠️  Timeout en iteración %d después de %d intentos\n", 
                   thread_id, i, retry_count);
            global_stats.increment_timeout();
        }
        
        usleep(1000);  // Pausa entre operaciones
    }
    
    printf("[Hilo %d] Terminado\n", thread_id);
    return nullptr;
}

// ============================================================================
// FUNCIONES DE BENCHMARK Y DEMOSTRACIÓN
// ============================================================================

/**
 * Demostrar deadlock intencional
 */
void demonstrate_deadlock() {
    printf("\n" + std::string(60, '=') + "\n");
    printf("🚨 DEMOSTRACIÓN DE DEADLOCK\n");
    printf(std::string(60, '=') + "\n");
    
    printf("ADVERTENCIA: Esta demostración puede colgarse (deadlock)\n");
    printf("Si no hay salida en 10 segundos, el programa está en deadlock.\n");
    printf("Use Ctrl+C para terminar o analice con gdb/pstack.\n\n");
    
    // Reset de recursos
    shared_resource_A = 0;
    shared_resource_B = 0;
    
    pthread_t thread1, thread2;
    int id1 = 1, id2 = 2;
    
    auto start = std::chrono::steady_clock::now();
    
    // Crear hilos con orden opuesto de adquisición de mutex
    pthread_create(&thread1, nullptr, thread_deadlock_1, &id1);
    pthread_create(&thread2, nullptr, thread_deadlock_2, &id2);
    
    // Intentar join con timeout simulado
    printf("Esperando terminación de hilos...\n");
    
    // En un sistema real, usaríamos pthread_timedjoin_np o señales para timeout
    pthread_join(thread1, nullptr);
    pthread_join(thread2, nullptr);
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    printf("✅ Hilos terminaron en %.2f segundos\n", duration);
    printf("Valores finales: A=%d, B=%d\n", shared_resource_A, shared_resource_B);
    
    if (duration > 5.0) {
        printf("⚠️  Tiempo sospechosamente largo - posible deadlock evitado por suerte\n");
    }
}

/**
 * Benchmark de la solución con orden total
 */
void benchmark_ordered_solution(int num_threads) {
    printf("\n" + std::string(60, '=') + "\n");
    printf("✅ SOLUCIÓN: ORDEN TOTAL DE MUTEX\n");
    printf(std::string(60, '=') + "\n");
    
    shared_resource_A = 0;
    shared_resource_B = 0;
    
    std::vector<pthread_t> threads(num_threads);
    std::vector<int> thread_ids(num_threads);
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < num_threads; i++) {
        thread_ids[i] = i + 1;
        pthread_create(&threads[i], nullptr, thread_ordered_lock, &thread_ids[i]);
    }
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], nullptr);
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    printf("⏱️  Tiempo total: %.3f segundos\n", duration);
    printf("📊 Valores finales: A=%d, B=%d\n", shared_resource_A, shared_resource_B);
    printf("🔒 Garantía: Sin deadlock por orden total\n");
}

/**
 * Benchmark de la solución con trylock y backoff
 */
void benchmark_trylock_solution(int num_threads) {
    printf("\n" + std::string(60, '=') + "\n");
    printf("🔄 SOLUCIÓN: TRYLOCK CON BACKOFF\n");
    printf(std::string(60, '=') + "\n");
    
    shared_resource_A = 0;
    shared_resource_B = 0;
    
    std::vector<pthread_t> threads(num_threads);
    std::vector<int> thread_ids(num_threads);
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < num_threads; i++) {
        thread_ids[i] = i + 1;
        pthread_create(&threads[i], nullptr, thread_trylock_backoff, &thread_ids[i]);
    }
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], nullptr);
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    printf("⏱️  Tiempo total: %.3f segundos\n", duration);
    printf("📊 Valores finales: A=%d, B=%d\n", shared_resource_A, shared_resource_B);
    printf("🔄 Flexibilidad: Permite diferentes órdenes de adquisición\n");
}

// ============================================================================
// FUNCIÓN PRINCIPAL
// ============================================================================

int main(int argc, char** argv) {
    printf("=== LABORATORIO 6 - PRÁCTICA 4: DEADLOCK ===\n");
    
    int num_threads = (argc > 1) ? std::atoi(argv[1]) : 4;
    bool skip_deadlock_demo = (argc > 2) && (std::atoi(argv[2]) == 1);
    
    printf("Configuración: %d hilos\n", num_threads);
    
    // Demostración de deadlock (opcional)
    if (!skip_deadlock_demo) {
        printf("\n¿Ejecutar demostración de deadlock? (puede colgar el programa)\n");
        printf("Presione Enter para continuar o Ctrl+C para omitir...\n");
        getchar();  // Esperar confirmación del usuario
        
        demonstrate_deadlock();
    } else {
        printf("Omitiendo demostración de deadlock (parámetro skip_deadlock=1)\n");
    }
    
    // Benchmarks de las soluciones
    benchmark_ordered_solution(num_threads);
    benchmark_trylock_solution(num_threads);
    
    // Estadísticas globales
    global_stats.print_stats();
    
    printf("\n" + std::string(60, '=') + "\n");
    printf("=== ANÁLISIS DE CONDICIONES DE COFFMAN ===\n");
    printf(std::string(60, '=') + "\n");
    printf("Para que ocurra deadlock se deben cumplir las 4 condiciones:\n\n");
    
    printf("1. 🔒 EXCLUSIÓN MUTUA\n");
    printf("   ✅ Los mutex solo permiten un hilo a la vez\n\n");
    
    printf("2. 🤝 HOLD AND WAIT\n");
    printf("   ✅ Los hilos mantienen un mutex mientras esperan otro\n\n");
    
    printf("3. 🚫 NO PREEMPTION\n");
    printf("   ✅ Los mutex no pueden ser quitados forzadamente\n\n");
    
    printf("4. 🔄 CIRCULAR WAIT\n");
    printf("   ✅ Hilo1 espera B (que tiene Hilo2), Hilo2 espera A (que tiene Hilo1)\n\n");
    
    printf("=== ESTRATEGIAS DE PREVENCIÓN ===\n\n");
    
    printf("🎯 ORDEN TOTAL (Rompe Circular Wait):\n");
    printf("   • Todos los hilos adquieren mutex en el mismo orden\n");
    printf("   • Garantiza ausencia de deadlock\n");
    printf("   • Puede reducir paralelismo\n\n");
    
    printf("🔄 TRYLOCK + BACKOFF (Rompe Hold and Wait):\n");
    printf("   • Si no puede adquirir segundo mutex, libera el primero\n");
    printf("   • Permite diferentes órdenes de adquisición\n");
    printf("   • Overhead por reintentos\n\n");
    
    printf("⏱️  TIMEOUT (Detección y recuperación):\n");
    printf("   • pthread_mutex_timedlock() con timeout\n");
    printf("   • Permite recuperación automática\n");
    printf("   • Requiere manejo de casos parciales\n\n");
    
    printf("=== HERRAMIENTAS DE DIAGNÓSTICO ===\n\n");
    printf("🔍 DETECCIÓN EN TIEMPO DE EJECUCIÓN:\n");
    printf("   gdb -p <pid>          # Attach a proceso colgado\n");
    printf("   (gdb) info threads    # Ver estado de todos los hilos\n");
    printf("   (gdb) thread <n>      # Cambiar a hilo específico\n");
    printf("   (gdb) bt              # Backtrace del hilo actual\n\n");
    
    printf("🧪 HERRAMIENTAS DE ANÁLISIS:\n");
    printf("   valgrind --tool=helgrind ./programa  # Detectar race conditions\n");
    printf("   valgrind --tool=drd ./programa       # Detector de deadlocks\n");
    printf("   strace -f ./programa                 # Trace de system calls\n\n");
    
    // Cleanup de recursos
    pthread_mutex_destroy(&mutex_A);
    pthread_mutex_destroy(&mutex_B);
    pthread_mutex_destroy(&global_stats.stats_mutex);
    
    printf("Programa terminado exitosamente.\n");
    return 0;
}