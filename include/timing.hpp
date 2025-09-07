/**
 * Universidad del Valle de Guatemala
 * CC3086 Programación de Microprocesadores
 * Laboratorio 6 - Sistema de Temporización
 * 
 * Autor: Sistema de Laboratorio
 * Fecha: Septiembre 2025
 * Propósito: Utilidades para medición precisa de tiempo y benchmarking
 */

#pragma once

#include <ctime>
#include <chrono>
#include <string>
#include <vector>
#include <cstdio>

// ============================================================================
// FUNCIONES BÁSICAS DE TEMPORIZACIÓN
// ============================================================================

/**
 * Obtener timestamp actual en segundos (monotonic clock)
 * Usa CLOCK_MONOTONIC para evitar saltos por ajustes del sistema
 */
inline double now_s() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/**
 * Obtener timestamp actual en milisegundos
 */
inline double now_ms() {
    return now_s() * 1000.0;
}

/**
 * Obtener timestamp actual en microsegundos
 */
inline double now_us() {
    return now_s() * 1000000.0;
}

/**
 * Obtener timestamp usando std::chrono (más portable)
 */
inline double now_chrono_s() {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration<double>(duration).count();
}

// ============================================================================
// CLASE PARA MEDICIONES DE RENDIMIENTO
// ============================================================================

/**
 * Clase para realizar benchmarks con múltiples mediciones y estadísticas
 */
class BenchmarkTimer {
private:
    std::vector<double> measurements;
    std::string name;
    double start_time;
    bool is_running;

public:
    explicit BenchmarkTimer(const std::string& benchmark_name) 
        : name(benchmark_name), start_time(0.0), is_running(false) {
        measurements.reserve(100);  // Pre-allocar para eficiencia
    }
    
    /**
     * Iniciar medición
     */
    void start() {
        start_time = now_s();
        is_running = true;
    }
    
    /**
     * Terminar medición y guardar resultado
     */
    double stop() {
        if (!is_running) {
            printf("Warning: Timer was not started for %s\n", name.c_str());
            return 0.0;
        }
        
        double elapsed = now_s() - start_time;
        measurements.push_back(elapsed);
        is_running = false;
        return elapsed;
    }
    
    /**
     * Medir una función automáticamente
     */
    template<typename Func>
    double measure(Func&& func) {
        start();
        func();
        return stop();
    }
    
    /**
     * Ejecutar múltiples mediciones de una función
     */
    template<typename Func>
    void benchmark(Func&& func, int iterations = 5) {
        printf("Ejecutando benchmark '%s' con %d iteraciones...\n", 
               name.c_str(), iterations);
        
        measurements.clear();
        measurements.reserve(iterations);
        
        for (int i = 0; i < iterations; i++) {
            printf("  Iteración %d/%d... ", i + 1, iterations);
            fflush(stdout);
            
            double time = measure(func);
            printf("%.6f seg\n", time);
        }
        
        print_statistics();
    }
    
    /**
     * Obtener tiempo promedio
     */
    double average() const {
        if (measurements.empty()) return 0.0;
        
        double sum = 0.0;
        for (double t : measurements) {
            sum += t;
        }
        return sum / measurements.size();
    }
    
    /**
     * Obtener tiempo mínimo
     */
    double minimum() const {
        if (measurements.empty()) return 0.0;
        
        double min_time = measurements[0];
        for (double t : measurements) {
            if (t < min_time) min_time = t;
        }
        return min_time;
    }
    
    /**
     * Obtener tiempo máximo
     */
    double maximum() const {
        if (measurements.empty()) return 0.0;
        
        double max_time = measurements[0];
        for (double t : measurements) {
            if (t > max_time) max_time = t;
        }
        return max_time;
    }
    
    /**
     * Calcular desviación estándar
     */
    double standard_deviation() const {
        if (measurements.size() < 2) return 0.0;
        
        double avg = average();
        double sum_sq_diff = 0.0;
        
        for (double t : measurements) {
            double diff = t - avg;
            sum_sq_diff += diff * diff;
        }
        
        return sqrt(sum_sq_diff / (measurements.size() - 1));
    }
    
    /**
     * Imprimir estadísticas completas
     */
    void print_statistics() const {
        if (measurements.empty()) {
            printf("No hay mediciones para '%s'\n", name.c_str());
            return;
        }
        
        printf("\n=== ESTADÍSTICAS: %s ===\n", name.c_str());
        printf("Mediciones: %zu\n", measurements.size());
        printf("Promedio:   %.6f seg\n", average());
        printf("Mínimo:     %.6f seg\n", minimum());
        printf("Máximo:     %.6f seg\n", maximum());
        printf("Desv. Est.: %.6f seg\n", standard_deviation());
        printf("Coef. Var.: %.2f%%\n", 100.0 * standard_deviation() / average());
        printf("\n");
    }
    
    /**
     * Exportar resultados a CSV
     */
    void export_to_csv(const std::string& filename) const {
        FILE* file = fopen(filename.c_str(), "w");
        if (!file) {
            printf("Error: No se pudo abrir archivo %s\n", filename.c_str());
            return;
        }
        
        fprintf(file, "benchmark,iteration,time_seconds\n");
        for (size_t i = 0; i < measurements.size(); i++) {
            fprintf(file, "%s,%zu,%.9f\n", name.c_str(), i + 1, measurements[i]);
        }
        
        fclose(file);
        printf("Resultados exportados a: %s\n", filename.c_str());
    }
    
    /**
     * Limpiar mediciones
     */
    void clear() {
        measurements.clear();
    }
    
    /**
     * Obtener todas las mediciones
     */
    const std::vector<double>& get_measurements() const {
        return measurements;
    }
};

// ============================================================================
// MACROS ÚTILES PARA TIMING
// ============================================================================

/**
 * Macro para medir tiempo de un bloque de código
 * Uso: TIME_BLOCK("nombre") { código_a_medir(); }
 */
#define TIME_BLOCK(name) \
    for (double _start = now_s(), _end = 0.0, _printed = 0.0; \
         _printed == 0.0 && (_end = now_s(), \
         printf("⏱️  %s: %.6f segundos\n", name, _end - _start), _printed = 1.0); \
         _printed = 1.0)

/**
 * Macro para medición simple con variable
 */
#define TIME_IT(code, time_var) \
    do { \
        double _start = now_s(); \
        code; \
        time_var = now_s() - _start; \
    } while(0)

// ============================================================================
// UTILIDADES ADICIONALES
// ============================================================================

/**
 * Formatear tiempo en unidades apropiadas
 */
inline std::string format_time(double seconds) {
    if (seconds >= 1.0) {
        return std::to_string(seconds) + " seg";
    } else if (seconds >= 0.001) {
        return std::to_string(seconds * 1000.0) + " ms";
    } else {
        return std::to_string(seconds * 1000000.0) + " μs";
    }
}

/**
 * Pausar ejecución por un número específico de microsegundos
 */
inline void sleep_us(int microseconds) {
    struct timespec ts;
    ts.tv_sec = microseconds / 1000000;
    ts.tv_nsec = (microseconds % 1000000) * 1000;
    nanosleep(&ts, nullptr);
}

/**
 * Pausar ejecución por un número específico de milisegundos
 */
inline void sleep_ms(int milliseconds) {
    sleep_us(milliseconds * 1000);
}