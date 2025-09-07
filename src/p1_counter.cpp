/**
 * Universidad del Valle de Guatemala
 * CC3086 Programación de Microprocesadores
 * Laboratorio 6 - Práctica 1: Contador con Race Conditions
 * 
 * Autor: Adrian Penagos
 * Fecha: Septiembre 2025
 * Propósito: Demostrar race conditions en contador global y comparar
 *           soluciones: naive, mutex, sharded y atomic
 */

#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <chrono>
#include <cassert>

// ============================================================================
// ESTRUCTURAS Y TIPOS
// ============================================================================

struct Args {
    long iters;                    // Iteraciones por hilo
    long* global;                  // Puntero a contador global
    pthread_mutex_t* mtx;          // Mutex para protección
    long* local_counter;           // Para versión sharded
    std::atomic<long>* atomic_counter; // Para versión atomic
};

// ============================================================================
// FUNCIONES WORKER PARA DIFERENTES ESTRATEGIAS
// ============================================================================

/**
 * Worker naive: Sin protección - RACE CONDITION INTENCIONAL
 * Esta función demuestra el problema de concurrencia
 */
void* worker_naive(void* p) {
    auto* a = static_cast<Args*>(p);
    
    for (long i = 0; i < a->iters; i++) {
        (*a->global)++;  // RACE CONDITION: incremento no atómico
        // El problema: load-increment-store no es operación atómica
        // Múltiples hilos pueden leer el mismo valor y sobrescribirse
    }
    
    return nullptr;
}

/**
 * Worker con mutex: Protección mediante exclusión mutua
 * Sección crítica protegida por pthread_mutex
 */
void* worker_mutex(void* p) {
    auto* a = static_cast<Args*>(p);
    
    for (long i = 0; i < a->iters; i++) {
        // SECCIÓN CRÍTICA: solo un hilo puede ejecutar este bloque
        pthread_mutex_lock(a->mtx);
        (*a->global)++;              // Operación protegida
        pthread_mutex_unlock(a->mtx);
        // Fin de sección crítica
    }
    
    return nullptr;
}

/**
 * Worker sharded: Cada hilo usa su propio contador local
 * Reduce contención al evitar compartir estado entre hilos
 */
void* worker_sharded(void* p) {
    auto* a = static_cast<Args*>(p);
    
    // Cada hilo incrementa su contador local (sin contención)
    for (long i = 0; i < a->iters; i++) {
        (*a->local_counter)++;  // Sin mutex necesario - cada hilo su variable
    }
    
    return nullptr;
}

/**
 * Worker atomic: Usa std::atomic para operaciones lock-free
 * Hardware garantiza atomicidad sin necesidad de locks explícitos
 */
void* worker_atomic(void* p) {
    auto* a = static_cast<Args*>(p);
    
    for (long i = 0; i < a->iters; i++) {
        // fetch_add es operación atómica implementada por hardware
        a->atomic_counter->fetch_add(1, std::memory_order_relaxed);
    }
    
    return nullptr;
}

// ============================================================================
// FUNCIÓN DE BENCHMARK
// ============================================================================

double benchmark_strategy(const char* strategy_name, 
                         void* (*worker_func)(void*), 
                         int num_threads, 
                         long iterations,
                         long expected_result) {
    
    printf("\n--- Benchmarking %s ---\n", strategy_name);
    printf("Threads: %d, Iterations per thread: %ld\n", num_threads, iterations);
    
    // Variables para diferentes estrategias
    long global_counter = 0;
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    std::vector<long> local_counters(num_threads, 0);
    std::atomic<long> atomic_counter{0};
    
    std::vector<pthread_t> threads(num_threads);
    std::vector<Args> args(num_threads);
    
    // Preparar argumentos para cada hilo
    for (int i = 0; i < num_threads; i++) {
        args[i] = {
            .iters = iterations,
            .global = &global_counter,
            .mtx = &mtx,
            .local_counter = &local_counters[i],
            .atomic_counter = &atomic_counter
        };
    }
    
    // Medir tiempo de ejecución
    auto start = std::chrono::high_resolution_clock::now();
    
    // Crear hilos
    for (int i = 0; i < num_threads; i++) {
        int result = pthread_create(&threads[i], nullptr, worker_func, &args[i]);
        assert(result == 0);  // Verificar creación exitosa
    }
    
    // Esperar terminación de todos los hilos
    for (int i = 0; i < num_threads; i++) {
        int result = pthread_join(threads[i], nullptr);
        assert(result == 0);  // Verificar join exitoso
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    // Calcular resultado final según la estrategia
    long final_result;
    if (worker_func == worker_sharded) {
        // Para sharded, sumar todos los contadores locales (fase reduce)
        final_result = 0;
        for (long local : local_counters) {
            final_result += local;
        }
    } else if (worker_func == worker_atomic) {
        final_result = atomic_counter.load();
    } else {
        final_result = global_counter;
    }
    
    // Reporte de resultados
    printf("Resultado: %ld (esperado: %ld)\n", final_result, expected_result);
    printf("Tiempo: %.6f segundos\n", duration);
    printf("Throughput: %.2f ops/seg\n", (double)(num_threads * iterations) / duration);
    
    if (final_result != expected_result) {
        printf("❌ INCONSISTENCIA DETECTADA - Diferencia: %ld\n", 
               expected_result - final_result);
    } else {
        printf("✅ Resultado correcto\n");
    }
    
    // Limpiar recursos
    pthread_mutex_destroy(&mtx);
    
    return duration;
}

// ============================================================================
// FUNCIÓN PRINCIPAL
// ============================================================================

int main(int argc, char** argv) {
    // Parámetros por defecto
    int num_threads = (argc > 1) ? std::atoi(argv[1]) : 4;
    long iterations = (argc > 2) ? std::atol(argv[2]) : 1000000;
    
    printf("=== LABORATORIO 6 - PRÁCTICA 1: RACE CONDITIONS ===\n");
    printf("Configuración: %d hilos, %ld iteraciones por hilo\n", 
           num_threads, iterations);
    
    long expected_total = (long)num_threads * iterations;
    
    // Ejecutar benchmarks para cada estrategia
    printf("\n🚨 ESTRATEGIA 1: NAIVE (Race condition intencional)\n");
    double time_naive = benchmark_strategy("Naive (Unsafe)", worker_naive, 
                                          num_threads, iterations, expected_total);
    
    printf("\n🔒 ESTRATEGIA 2: MUTEX (Exclusión mutua)\n");
    double time_mutex = benchmark_strategy("Mutex", worker_mutex, 
                                          num_threads, iterations, expected_total);
    
    printf("\n📊 ESTRATEGIA 3: SHARDED (Contadores particionados)\n");
    double time_sharded = benchmark_strategy("Sharded", worker_sharded, 
                                           num_threads, iterations, expected_total);
    
    printf("\n⚡ ESTRATEGIA 4: ATOMIC (Lock-free)\n");
    double time_atomic = benchmark_strategy("Atomic", worker_atomic, 
                                          num_threads, iterations, expected_total);
    
    // Análisis comparativo
    printf("\n=== ANÁLISIS COMPARATIVO ===\n");
    printf("Tiempo Naive:   %.6f seg (baseline)\n", time_naive);
    printf("Tiempo Mutex:   %.6f seg (%.2fx más lento)\n", 
           time_mutex, time_mutex / time_naive);
    printf("Tiempo Sharded: %.6f seg (%.2fx vs naive)\n", 
           time_sharded, time_sharded / time_naive);
    printf("Tiempo Atomic:  %.6f seg (%.2fx vs naive)\n", 
           time_atomic, time_atomic / time_naive);
    
    printf("\n=== OBSERVACIONES ===\n");
    printf("• Naive: Más rápido pero resultados incorrectos (race condition)\n");
    printf("• Mutex: Correcto pero con overhead de sincronización\n");
    printf("• Sharded: Reduce contención, pero requiere fase reduce\n");
    printf("• Atomic: Lock-free, balance entre rendimiento y simplicidad\n");
    
    return 0;
}