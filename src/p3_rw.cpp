/**
 * Universidad del Valle de Guatemala
 * CC3086 Programación de Microprocesadores
 * Laboratorio 6 - Práctica 3: Lectores/Escritores con HashMap
 * 
 * Autor: Adrian Penagos
 * Fecha: Septiembre 2025
 * Propósito: Comparar pthread_rwlock_t vs pthread_mutex_t en tabla hash
 *           Evaluar rendimiento bajo diferentes proporciones de lectura/escritura
 */

#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <cassert>
#include <cstring>
#include <unistd.h>
#include <random>

// ============================================================================
// CONSTANTES Y CONFIGURACIÓN
// ============================================================================

constexpr int NUM_BUCKETS = 1024;        // Número de buckets en la tabla hash
constexpr int KEY_RANGE = 10000;         // Rango de claves posibles
constexpr int INITIAL_ENTRIES = 500;     // Entradas iniciales en la tabla

// ============================================================================
// ESTRUCTURA DE NODO PARA HASH MAP
// ============================================================================

struct Node {
    int key;
    int value;
    Node* next;
    
    Node(int k, int v) : key(k), value(v), next(nullptr) {}
};

// ============================================================================
// IMPLEMENTACIONES DE HASH MAP
// ============================================================================

/**
 * HashMap con pthread_mutex_t (exclusión mutua total)
 * Todas las operaciones son mutuamente excluyentes
 */
class MutexHashMap {
private:
    Node* buckets[NUM_BUCKETS];
    pthread_mutex_t mutex;
    
    // Función hash simple
    int hash(int key) const {
        return ((unsigned int)key * 2654435761U) % NUM_BUCKETS;
    }

public:
    // Estadísticas de monitoreo
    long reads = 0;
    long writes = 0;
    long read_blocks = 0;
    long write_blocks = 0;

    MutexHashMap() {
        memset(buckets, 0, sizeof(buckets));
        pthread_mutex_init(&mutex, nullptr);
    }
    
    ~MutexHashMap() {
        // Limpiar todas las listas enlazadas
        for (int i = 0; i < NUM_BUCKETS; i++) {
            Node* current = buckets[i];
            while (current) {
                Node* next = current->next;
                delete current;
                current = next;
            }
        }
        pthread_mutex_destroy(&mutex);
    }
    
    /**
     * Buscar valor por clave (operación de lectura)
     * Con mutex, bloquea TODA la tabla incluso para lecturas
     */
    bool get(int key, int* value) {
        pthread_mutex_lock(&mutex);  // BLOQUEO EXCLUSIVO TOTAL
        reads++;
        
        int bucket_idx = hash(key);
        Node* current = buckets[bucket_idx];
        
        // Búsqueda lineal en la lista del bucket
        while (current) {
            if (current->key == key) {
                *value = current->value;
                pthread_mutex_unlock(&mutex);
                return true;
            }
            current = current->next;
        }
        
        pthread_mutex_unlock(&mutex);
        return false;  // Clave no encontrada
    }
    
    /**
     * Insertar o actualizar clave-valor (operación de escritura)
     */
    void put(int key, int value) {
        pthread_mutex_lock(&mutex);  // BLOQUEO EXCLUSIVO TOTAL
        writes++;
        
        int bucket_idx = hash(key);
        Node* current = buckets[bucket_idx];
        
        // Buscar si la clave ya existe
        while (current) {
            if (current->key == key) {
                current->value = value;  // Actualizar valor existente
                pthread_mutex_unlock(&mutex);
                return;
            }
            current = current->next;
        }
        
        // Insertar nueva entrada al inicio de la lista
        Node* new_node = new Node(key, value);
        new_node->next = buckets[bucket_idx];
        buckets[bucket_idx] = new_node;
        
        pthread_mutex_unlock(&mutex);
    }
    
    /**
     * Eliminar entrada por clave
     */
    bool remove(int key) {
        pthread_mutex_lock(&mutex);
        writes++;
        
        int bucket_idx = hash(key);
        Node* current = buckets[bucket_idx];
        Node* prev = nullptr;
        
        while (current) {
            if (current->key == key) {
                if (prev) {
                    prev->next = current->next;
                } else {
                    buckets[bucket_idx] = current->next;
                }
                delete current;
                pthread_mutex_unlock(&mutex);
                return true;
            }
            prev = current;
            current = current->next;
        }
        
        pthread_mutex_unlock(&mutex);
        return false;
    }
    
    void get_stats(long* r, long* w, long* rb, long* wb) {
        pthread_mutex_lock(&mutex);
        *r = reads; *w = writes; *rb = read_blocks; *wb = write_blocks;
        pthread_mutex_unlock(&mutex);
    }
};

/**
 * HashMap con pthread_rwlock_t (lectores concurrentes)
 * Múltiples lectores pueden acceder simultáneamente
 * Escritores tienen acceso exclusivo
 */
class RWLockHashMap {
private:
    Node* buckets[NUM_BUCKETS];
    pthread_rwlock_t rwlock;
    
    int hash(int key) const {
        return ((unsigned int)key * 2654435761U) % NUM_BUCKETS;
    }

public:
    // Estadísticas de monitoreo
    long reads = 0;
    long writes = 0;
    long read_blocks = 0;
    long write_blocks = 0;

    RWLockHashMap() {
        memset(buckets, 0, sizeof(buckets));
        pthread_rwlock_init(&rwlock, nullptr);
    }
    
    ~RWLockHashMap() {
        for (int i = 0; i < NUM_BUCKETS; i++) {
            Node* current = buckets[i];
            while (current) {
                Node* next = current->next;
                delete current;
                current = next;
            }
        }
        pthread_rwlock_destroy(&rwlock);
    }
    
    /**
     * Buscar valor por clave (operación de lectura)
     * Usa READ LOCK - permite múltiples lectores concurrentes
     */
    bool get(int key, int* value) {
        pthread_rwlock_rdlock(&rwlock);  // BLOQUEO COMPARTIDO PARA LECTURA
        reads++;
        
        int bucket_idx = hash(key);
        Node* current = buckets[bucket_idx];
        
        while (current) {
            if (current->key == key) {
                *value = current->value;
                pthread_rwlock_unlock(&rwlock);
                return true;
            }
            current = current->next;
        }
        
        pthread_rwlock_unlock(&rwlock);
        return false;
    }
    
    /**
     * Insertar o actualizar clave-valor (operación de escritura)
     * Usa WRITE LOCK - acceso exclusivo total
     */
    void put(int key, int value) {
        pthread_rwlock_wrlock(&rwlock);  // BLOQUEO EXCLUSIVO PARA ESCRITURA
        writes++;
        
        int bucket_idx = hash(key);
        Node* current = buckets[bucket_idx];
        
        // Buscar si la clave ya existe
        while (current) {
            if (current->key == key) {
                current->value = value;
                pthread_rwlock_unlock(&rwlock);
                return;
            }
            current = current->next;
        }
        
        // Insertar nueva entrada
        Node* new_node = new Node(key, value);
        new_node->next = buckets[bucket_idx];
        buckets[bucket_idx] = new_node;
        
        pthread_rwlock_unlock(&rwlock);
    }
    
    /**
     * Eliminar entrada por clave
     */
    bool remove(int key) {
        pthread_rwlock_wrlock(&rwlock);  // BLOQUEO EXCLUSIVO
        writes++;
        
        int bucket_idx = hash(key);
        Node* current = buckets[bucket_idx];
        Node* prev = nullptr;
        
        while (current) {
            if (current->key == key) {
                if (prev) {
                    prev->next = current->next;
                } else {
                    buckets[bucket_idx] = current->next;
                }
                delete current;
                pthread_rwlock_unlock(&rwlock);
                return true;
            }
            prev = current;
            current = current->next;
        }
        
        pthread_rwlock_unlock(&rwlock);
        return false;
    }
    
    void get_stats(long* r, long* w, long* rb, long* wb) {
        pthread_rwlock_rdlock(&rwlock);
        *r = reads; *w = writes; *rb = read_blocks; *wb = write_blocks;
        pthread_rwlock_unlock(&rwlock);
    }
};

// ============================================================================
// HILOS WORKER PARA BENCHMARKS
// ============================================================================

struct WorkerArgs {
    void* hashmap;           // Puntero a MutexHashMap o RWLockHashMap
    bool is_rwlock;          // true si es RWLockHashMap, false si es MutexHashMap
    int thread_id;
    long operations;
    int read_percentage;     // 90 = 90% lecturas, 10% escrituras
    std::mt19937* rng;       // Generador de números aleatorios
};

/**
 * Worker thread que ejecuta mezcla de operaciones de lectura/escritura
 */
void* worker_thread(void* arg) {
    WorkerArgs* args = static_cast<WorkerArgs*>(arg);
    std::uniform_int_distribution<int> key_dist(0, KEY_RANGE - 1);
    std::uniform_int_distribution<int> op_dist(0, 99);  // 0-99 para porcentajes
    std::uniform_int_distribution<int> val_dist(1, 1000);
    
    for (long i = 0; i < args->operations; i++) {
        int key = key_dist(*args->rng);
        bool is_read = op_dist(*args->rng) < args->read_percentage;
        
        if (is_read) {
            // Operación de lectura
            int value;
            if (args->is_rwlock) {
                static_cast<RWLockHashMap*>(args->hashmap)->get(key, &value);
            } else {
                static_cast<MutexHashMap*>(args->hashmap)->get(key, &value);
            }
        } else {
            // Operación de escritura (inserción/actualización)
            int value = val_dist(*args->rng);
            if (args->is_rwlock) {
                static_cast<RWLockHashMap*>(args->hashmap)->put(key, value);
            } else {
                static_cast<MutexHashMap*>(args->hashmap)->put(key, value);
            }
        }
    }
    
    return nullptr;
}

// ============================================================================
// FUNCIONES DE BENCHMARK
// ============================================================================

template<typename HashMap>
double benchmark_hashmap(const char* name, HashMap* map, bool is_rwlock,
                        int num_threads, long ops_per_thread, int read_pct) {
    
    printf("\n--- Benchmarking %s (R/W: %d/%d%%) ---\n", 
           name, read_pct, 100 - read_pct);
    
    std::vector<pthread_t> threads(num_threads);
    std::vector<WorkerArgs> args(num_threads);
    std::vector<std::mt19937> rngs(num_threads);
    
    // Inicializar generadores de números aleatorios con diferentes semillas
    for (int i = 0; i < num_threads; i++) {
        rngs[i].seed(42 + i);  // Semillas diferentes pero reproducibles
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Crear hilos trabajadores
    for (int i = 0; i < num_threads; i++) {
        args[i] = {
            .hashmap = map,
            .is_rwlock = is_rwlock,
            .thread_id = i,
            .operations = ops_per_thread,
            .read_percentage = read_pct,
            .rng = &rngs[i]
        };
        
        pthread_create(&threads[i], nullptr, worker_thread, &args[i]);
    }
    
    // Esperar terminación
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], nullptr);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    // Obtener estadísticas
    long reads, writes, read_blocks, write_blocks;
    map->get_stats(&reads, &writes, &read_blocks, &write_blocks);
    
    long total_ops = reads + writes;
    double throughput = total_ops / duration;
    
    printf("Tiempo: %.4f seg\n", duration);
    printf("Operaciones totales: %ld (R: %ld, W: %ld)\n", total_ops, reads, writes);
    printf("Throughput: %.2f ops/seg\n", throughput);
    printf("Proporción real R/W: %.1f%%/%.1f%%\n", 
           100.0 * reads / total_ops, 100.0 * writes / total_ops);
    
    return throughput;
}

/**
 * Poblar el hashmap con datos iniciales
 */
template<typename HashMap>
void populate_hashmap(HashMap* map, int num_entries) {
    std::mt19937 rng(42);  // Semilla fija para reproducibilidad
    std::uniform_int_distribution<int> key_dist(0, KEY_RANGE - 1);
    std::uniform_int_distribution<int> val_dist(1, 1000);
    
    for (int i = 0; i < num_entries; i++) {
        int key = key_dist(rng);
        int value = val_dist(rng);
        map->put(key, value);
    }
}

// ============================================================================
// FUNCIÓN PRINCIPAL
// ============================================================================

int main(int argc, char** argv) {
    printf("=== LABORATORIO 6 - PRÁCTICA 3: LECTORES/ESCRITORES ===\n");
    
    // Parámetros configurables
    int num_threads = (argc > 1) ? std::atoi(argv[1]) : 4;
    long ops_per_thread = (argc > 2) ? std::atol(argv[2]) : 100000;
    
    printf("Configuración: %d hilos, %ld ops/hilo\n", num_threads, ops_per_thread);
    printf("Buckets: %d, Rango de claves: %d\n", NUM_BUCKETS, KEY_RANGE);
    
    // Diferentes proporciones de lectura/escritura para probar
    std::vector<int> read_percentages = {90, 70, 50, 30, 10};
    
    printf("\n=== COMPARACIÓN MUTEX vs RWLOCK ===\n");
    
        for (int read_pct : read_percentages) {
        printf("\n============================================================\n");
        printf("PROPORCIÓN: %d%% LECTURAS, %d%% ESCRITURAS\n", read_pct, 100 - read_pct);
        printf("============================================================\n");
        
        // Test con Mutex HashMap
        MutexHashMap mutex_map;
        populate_hashmap(&mutex_map, INITIAL_ENTRIES);
        double mutex_throughput = benchmark_hashmap("Mutex HashMap", &mutex_map, false,
                                                   num_threads, ops_per_thread, read_pct);
        
        // Test con RWLock HashMap  
        RWLockHashMap rwlock_map;
        populate_hashmap(&rwlock_map, INITIAL_ENTRIES);
        double rwlock_throughput = benchmark_hashmap("RWLock HashMap", &rwlock_map, true,
                                                    num_threads, ops_per_thread, read_pct);
        
        // Análisis comparativo
        double speedup = rwlock_throughput / mutex_throughput;
        printf("\n--- ANÁLISIS ---\n");
        printf("Speedup RWLock vs Mutex: %.2fx\n", speedup);
        
        if (speedup > 1.1) {
            printf("✅ RWLock es significativamente mejor (%.1f%% más rápido)\n", 
                   (speedup - 1) * 100);
        } else if (speedup < 0.9) {
            printf("❌ RWLock es peor (%.1f%% más lento)\n", 
                   (1 - speedup) * 100);
        } else {
            printf("⚖️  Rendimiento similar (diferencia < 10%%)\n");
        }
    }
    
    printf("\n============================================================\n");
    printf("=== CONCLUSIONES ===\n");
    printf("• RWLock conviene cuando > 70%% son lecturas\n");
    printf("• Mutex puede ser mejor con muchas escrituras (menos overhead)\n");
    printf("• El tamaño del bucket afecta la contención:\n");
    printf("  - Más buckets = menos colisiones = menos contención\n");
    printf("  - Menos buckets = más colisiones = más contención\n");
    
    printf("\n=== PREGUNTAS GUÍA RESPONDIDAS ===\n");
    printf("• ¿Cuándo conviene rwlock? → Cuando hay mayoría de lecturas (> 70%%)\n");
    printf("• ¿Cómo evitar starvation? → Usar PTHREAD_RWLOCK_PREFER_WRITER_NP\n");
    printf("• ¿Impacto del bucket size? → Más buckets = menos contención\n");
    
    printf("\n=== POLÍTICAS DE EQUIDAD ===\n");
    printf("• Reader-preference: Puede causar writer starvation\n");
    printf("• Writer-preference: Previene starvation pero reduce concurrencia\n");
    printf("• FIFO: Más justo pero más complejo de implementar\n");
    
    return 0;
}