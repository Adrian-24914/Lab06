#!/bin/bash

# Universidad del Valle de Guatemala
# CC3086 Programación de Microprocesadores
# Laboratorio 6 - Script de Ejecución Automatizada
# Autor: Adrian Penagos
# Fecha: Septiembre 2025

set -e  # Terminar en caso de error

# ============================================================================
# CONFIGURACIÓN Y CONSTANTES
# ============================================================================

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Directorios
BIN_DIR="bin"
DATA_DIR="data"
SCRIPTS_DIR="scripts"
DOCS_DIR="docs"

# Configuración por defecto
DEFAULT_THREADS=4
DEFAULT_ITERATIONS=100000
TIMEOUT_SECONDS=30
BENCHMARK_REPETITIONS=3

# Log file
LOG_FILE="${DATA_DIR}/execution_log.txt"
RESULTS_FILE="${DATA_DIR}/benchmark_results.csv"

# ============================================================================
# FUNCIONES AUXILIARES
# ============================================================================

# Función para logging con timestamp
log() {
    local level=$1
    shift
    local message="$@"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] [$level] $message" >> "$LOG_FILE"
    
    case $level in
        "INFO")  echo -e "${CYAN}[INFO]${NC} $message" ;;
        "WARN")  echo -e "${YELLOW}[WARN]${NC} $message" ;;
        "ERROR") echo -e "${RED}[ERROR]${NC} $message" ;;
        "SUCCESS") echo -e "${GREEN}[SUCCESS]${NC} $message" ;;
        "HEADER") echo -e "${PURPLE}[HEADER]${NC} $message" ;;
    esac
}

# Función para verificar que un ejecutable existe
check_executable() {
    local exec_path="$1"
    if [[ ! -x "$exec_path" ]]; then
        log "ERROR" "Ejecutable no encontrado: $exec_path"
        log "INFO" "Ejecute 'make' para compilar primero"
        exit 1
    fi
}

# Función para ejecutar comando con timeout
execute_with_timeout() {
    local timeout_sec=$1
    local description=$2
    shift 2
    local command="$@"
    
    log "INFO" "Ejecutando: $description"
    log "INFO" "Comando: $command"
    
    if timeout "$timeout_sec" bash -c "$command"; then
        log "SUCCESS" "$description completado exitosamente"
        return 0
    else
        local exit_code=$?
        if [[ $exit_code -eq 124 ]]; then
            log "WARN" "$description terminado por timeout (${timeout_sec}s)"
        else
            log "ERROR" "$description falló con código $exit_code"
        fi
        return $exit_code
    fi
}

# Función para crear encabezado de sección
print_section() {
    local title=$1
    local width=80
    local padding=$(( (width - ${#title} - 2) / 2 ))
    
    echo ""
    echo "$(printf '=%.0s' $(seq 1 $width))"
    echo "$(printf '%*s' $padding)$title"
    echo "$(printf '=%.0s' $(seq 1 $width))"
    echo ""
}

# Función para mostrar ayuda
show_help() {
    cat << EOF
Universidad del Valle de Guatemala - CC3086 Lab06
Script de Ejecución Automatizada

USAGE: $0 [OPCIONES] [PRÁCTICA]

OPCIONES:
    -h, --help          Mostrar esta ayuda
    -t, --threads N     Número de threads (default: $DEFAULT_THREADS)
    -i, --iterations N  Iteraciones por thread (default: $DEFAULT_ITERATIONS)
    -r, --repetitions N Repeticiones para benchmark (default: $BENCHMARK_REPETITIONS)
    -s, --skip-deadlock Omitir demostración de deadlock
    -b, --benchmark     Solo ejecutar benchmarks
    -q, --quick         Ejecución rápida con parámetros reducidos
    --timeout N         Timeout en segundos (default: $TIMEOUT_SECONDS)
    --clean             Limpiar datos previos antes de ejecutar

PRÁCTICAS:
    p1 | counter        Práctica 1: Race conditions en contador
    p2 | ring           Práctica 2: Búfer circular productor-consumidor
    p3 | rw             Práctica 3: Lectores/Escritores con HashMap
    p4 | deadlock       Práctica 4: Deadlock intencional y corrección
    p5 | pipeline       Práctica 5: Pipeline con barreras
    all                 Ejecutar todas las prácticas (default)

EJEMPLOS:
    $0                              # Ejecutar todas las prácticas
    $0 p1                          # Solo práctica 1
    $0 -t 8 -i 1000000 p3         # Práctica 3 con 8 threads
    $0 --benchmark                 # Solo benchmarks de todas las prácticas
    $0 --quick p4                  # Práctica 4 con parámetros reducidos
    $0 --clean --benchmark         # Limpiar y hacer benchmark completo

EOF
}

# Función para configurar entorno
setup_environment() {
    log "INFO" "Configurando entorno de ejecución"
    
    # Crear directorios necesarios
    mkdir -p "$DATA_DIR" "$SCRIPTS_DIR" "$DOCS_DIR" "$BIN_DIR"
    
    # Crear archivo de log
    echo "=== Lab06 Execution Log ===" > "$LOG_FILE"
    echo "Start time: $(date)" >> "$LOG_FILE"
    echo "User: $(whoami)" >> "$LOG_FILE"
    echo "System: $(uname -a)" >> "$LOG_FILE"
    echo "Threads available: $(nproc)" >> "$LOG_FILE"
    echo "" >> "$LOG_FILE"
    
    # Crear header CSV para resultados
    if [[ ! -f "$RESULTS_FILE" ]] || [[ "$CLEAN_DATA" == "true" ]]; then
        echo "practice,config,threads,iterations,time_seconds,throughput_ops_sec,additional_info" > "$RESULTS_FILE"
    fi
    
    log "SUCCESS" "Entorno configurado exitosamente"
}

# ============================================================================
# FUNCIONES DE EJECUCIÓN POR PRÁCTICA
# ============================================================================

run_practice1() {
    local threads=$1
    local iterations=$2
    
    print_section "PRÁCTICA 1: RACE CONDITIONS EN CONTADOR"
    
    local exec_path="${BIN_DIR}/p1_counter"
    check_executable "$exec_path"
    
    log "INFO" "Configuración: $threads threads, $iterations iteraciones/thread"
    
    execute_with_timeout $TIMEOUT_SECONDS \
        "Práctica 1 - Contador con race conditions" \
        "$exec_path $threads $iterations"
    
    # Si es benchmark, ejecutar múltiples veces y promediar
    if [[ "$BENCHMARK_MODE" == "true" ]]; then
        log "INFO" "Ejecutando benchmark de Práctica 1..."
        python3 "${SCRIPTS_DIR}/bench.py" p1_counter "$threads" "$iterations" "$BENCHMARK_REPETITIONS"
    fi
}

run_practice2() {
    local threads=$1
    local iterations=$2
    
    print_section "PRÁCTICA 2: BÚFER CIRCULAR PRODUCTOR-CONSUMIDOR"
    
    local exec_path="${BIN_DIR}/p2_ring"
    check_executable "$exec_path"
    
    # Para práctica 2, dividir threads entre productores y consumidores
    local producers=$((threads / 2))
    local consumers=$((threads - producers))
    [[ $producers -eq 0 ]] && producers=1
    [[ $consumers -eq 0 ]] && consumers=1
    
    log "INFO" "Configuración: ${producers}P/${consumers}C, $iterations items/productor"
    
    execute_with_timeout $TIMEOUT_SECONDS \
        "Práctica 2 - Búfer circular" \
        "$exec_path $producers $consumers $iterations"
    
    if [[ "$BENCHMARK_MODE" == "true" ]]; then
        log "INFO" "Ejecutando benchmark de Práctica 2..."
        python3 "${SCRIPTS_DIR}/bench.py" p2_ring "$producers $consumers" "$iterations" "$BENCHMARK_REPETITIONS"
    fi
}

run_practice3() {
    local threads=$1
    local iterations=$2
    
    print_section "PRÁCTICA 3: LECTORES/ESCRITORES CON HASHMAP"
    
    local exec_path="${BIN_DIR}/p3_rw"
    check_executable "$exec_path"
    
    log "INFO" "Configuración: $threads threads, $iterations ops/thread"
    
    execute_with_timeout $TIMEOUT_SECONDS \
        "Práctica 3 - Lectores/Escritores" \
        "$exec_path $threads $iterations"
    
    if [[ "$BENCHMARK_MODE" == "true" ]]; then
        log "INFO" "Ejecutando benchmark de Práctica 3..."
        python3 "${SCRIPTS_DIR}/bench.py" p3_rw "$threads" "$iterations" "$BENCHMARK_REPETITIONS"
    fi
}

run_practice4() {
    local threads=$1
    
    print_section "PRÁCTICA 4: DEADLOCK INTENCIONAL Y CORRECCIÓN"
    
    local exec_path="${BIN_DIR}/p4_deadlock"
    check_executable "$exec_path"
    
    log "INFO" "Configuración: $threads threads"
    
    if [[ "$SKIP_DEADLOCK" == "true" ]]; then
        log "WARN" "Omitiendo demostración de deadlock (--skip-deadlock)"
        execute_with_timeout $TIMEOUT_SECONDS \
            "Práctica 4 - Solo correcciones" \
            "$exec_path $threads 1"
    else
        log "WARN" "⚠️  ADVERTENCIA: Esta práctica puede causar deadlock intencional"
        log "INFO" "Si el programa se cuelga más de ${TIMEOUT_SECONDS}s, será terminado automáticamente"
        
        execute_with_timeout $TIMEOUT_SECONDS \
            "Práctica 4 - Deadlock y correcciones" \
            "$exec_path $threads 0" || true  # Permitir timeout sin fallar
    fi
    
    if [[ "$BENCHMARK_MODE" == "true" ]]; then
        log "INFO" "Ejecutando benchmark de Práctica 4 (solo correcciones)..."
        python3 "${SCRIPTS_DIR}/bench.py" p4_deadlock "$threads 1" "0" "$BENCHMARK_REPETITIONS"
    fi
}

run_practice5() {
    local iterations=$2  # Para pipeline, iterations es más relevante que threads
    [[ -z "$iterations" ]] && iterations=1000
    
    print_section "PRÁCTICA 5: PIPELINE CON BARRERAS"
    
    local exec_path="${BIN_DIR}/p5_pipeline"
    check_executable "$exec_path"
    
    log "INFO" "Configuración: Pipeline de 3 etapas, $iterations ticks"
    
    execute_with_timeout $TIMEOUT_SECONDS \
        "Práctica 5 - Pipeline con barreras" \
        "$exec_path $iterations"
    
    if [[ "$BENCHMARK_MODE" == "true" ]]; then
        log "INFO" "Ejecutando benchmark de Práctica 5..."
        python3 "${SCRIPTS_DIR}/bench.py" p5_pipeline "3" "$iterations" "$BENCHMARK_REPETITIONS"
    fi
}

# ============================================================================
# FUNCIÓN PRINCIPAL DE EJECUCIÓN
# ============================================================================

run_all_practices() {
    local threads=$1
    local iterations=$2
    
    print_section "EJECUTANDO TODAS LAS PRÁCTICAS"
    log "INFO" "Configuración global: $threads threads, $iterations iteraciones"
    
    # Ejecutar cada práctica
    run_practice1 "$threads" "$iterations"
    run_practice2 "$threads" "$iterations"
    run_practice3 "$threads" "$iterations"
    run_practice4 "$threads"
    run_practice5 "$threads" "$iterations"
    
    # Generar resumen final
    generate_summary
}

# Función para generar resumen de ejecución
generate_summary() {
    print_section "RESUMEN DE EJECUCIÓN"
    
    log "HEADER" "Resultados guardados en:"
    echo "  • Log completo: $LOG_FILE"
    echo "  • Datos CSV: $RESULTS_FILE"
    echo "  • Datos adicionales: $DATA_DIR/"
    
    if [[ -f "$RESULTS_FILE" ]]; then
        log "INFO" "Últimos resultados de benchmark:"
        tail -5 "$RESULTS_FILE" | column -t -s ','
    fi
    
    # Verificar archivos generados
    log "INFO" "Archivos generados en $DATA_DIR:"
    ls -la "$DATA_DIR/" || log "WARN" "No se encontraron archivos en $DATA_DIR"
    
    # Mostrar estadísticas del sistema
    log "INFO" "Uso de recursos durante la ejecución:"
    echo "  • Carga promedio: $(uptime | awk '{print $NF}')"
    echo "  • Memoria libre: $(free -h | grep Mem | awk '{print $7}')"
    echo "  • Espacio en disco: $(df -h . | tail -1 | awk '{print $4}') disponible"
    
    log "SUCCESS" "Ejecución completada exitosamente"
}

# ============================================================================
# PARSEO DE ARGUMENTOS DE LÍNEA DE COMANDOS
# ============================================================================

parse_arguments() {
    THREADS="$DEFAULT_THREADS"
    ITERATIONS="$DEFAULT_ITERATIONS"
    PRACTICE="all"
    SKIP_DEADLOCK="false"
    BENCHMARK_MODE="false"
    QUICK_MODE="false"
    CLEAN_DATA="false"
    TIMEOUT_SECONDS=30
    
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            -t|--threads)
                THREADS="$2"
                shift 2
                ;;
            -i|--iterations)
                ITERATIONS="$2"
                shift 2
                ;;
            -r|--repetitions)
                BENCHMARK_REPETITIONS="$2"
                shift 2
                ;;
            -s|--skip-deadlock)
                SKIP_DEADLOCK="true"
                shift
                ;;
            -b|--benchmark)
                BENCHMARK_MODE="true"
                shift
                ;;
            -q|--quick)
                QUICK_MODE="true"
                THREADS=2
                ITERATIONS=10000
                BENCHMARK_REPETITIONS=2
                TIMEOUT_SECONDS=15
                shift
                ;;
            --timeout)
                TIMEOUT_SECONDS="$2"
                shift 2
                ;;
            --clean)
                CLEAN_DATA="true"
                shift
                ;;
            p1|counter)
                PRACTICE="p1"
                shift
                ;;
            p2|ring)
                PRACTICE="p2"
                shift
                ;;
            p3|rw)
                PRACTICE="p3"
                shift
                ;;
            p4|deadlock)
                PRACTICE="p4"
                shift
                ;;
            p5|pipeline)
                PRACTICE="p5"
                shift
                ;;
            all)
                PRACTICE="all"
                shift
                ;;
            *)
                log "ERROR" "Opción desconocida: $1"
                echo "Use --help para ver opciones disponibles"
                exit 1
                ;;
        esac
    done
    
    # Validar argumentos
    if ! [[ "$THREADS" =~ ^[0-9]+$ ]] || [[ "$THREADS" -lt 1 ]]; then
        log "ERROR" "Número de threads inválido: $THREADS"
        exit 1
    fi
    
    if ! [[ "$ITERATIONS" =~ ^[0-9]+$ ]] || [[ "$ITERATIONS" -lt 1 ]]; then
        log "ERROR" "Número de iteraciones inválido: $ITERATIONS"
        exit 1
    fi
    
    if ! [[ "$BENCHMARK_REPETITIONS" =~ ^[0-9]+$ ]] || [[ "$BENCHMARK_REPETITIONS" -lt 1 ]]; then
        log "ERROR" "Número de repeticiones inválido: $BENCHMARK_REPETITIONS"
        exit 1
    fi
}

# ============================================================================
# FUNCIÓN PRINCIPAL
# ============================================================================

main() {
    # Banner inicial
    cat << "EOF"
    
╔══════════════════════════════════════════════════════════════╗
║                Universidad del Valle de Guatemala             ║
║           CC3086 - Programación de Microprocesadores        ║
║              Laboratorio 6 - Execution Runner               ║
║                                                              ║
║    🧵 Concurrencia • 🔒 Sincronización • ⚡ Rendimiento    ║
╚══════════════════════════════════════════════════════════════╝

EOF

    # Parsear argumentos
    parse_arguments "$@"
    
    # Mostrar configuración
    log "HEADER" "CONFIGURACIÓN DE EJECUCIÓN"
    echo "  • Práctica: $PRACTICE"
    echo "  • Threads: $THREADS"
    echo "  • Iteraciones: $ITERATIONS"
    echo "  • Repeticiones benchmark: $BENCHMARK_REPETITIONS"
    echo "  • Timeout: ${TIMEOUT_SECONDS}s"
    echo "  • Modo rápido: $QUICK_MODE"
    echo "  • Solo benchmark: $BENCHMARK_MODE"
    echo "  • Skip deadlock: $SKIP_DEADLOCK"
    echo "  • Limpiar datos: $CLEAN_DATA"
    echo ""
    
    # Configurar entorno
    setup_environment
    
    # Limpiar datos previos si se solicita
    if [[ "$CLEAN_DATA" == "true" ]]; then
        log "INFO" "Limpiando datos previos..."
        rm -f "${DATA_DIR}"/*.csv "${DATA_DIR}"/*.txt "${DATA_DIR}"/*.log
        echo "practice,config,threads,iterations,time_seconds,throughput_ops_sec,additional_info" > "$RESULTS_FILE"
    fi
    
    # Verificar que existen los ejecutables necesarios
    if [[ "$PRACTICE" == "all" ]]; then
        check_executable "${BIN_DIR}/p1_counter"
        check_executable "${BIN_DIR}/p2_ring"
        check_executable "${BIN_DIR}/p3_rw"
        check_executable "${BIN_DIR}/p4_deadlock"
        check_executable "${BIN_DIR}/p5_pipeline"
    fi
    
    # Ejecutar según la práctica seleccionada
    case $PRACTICE in
        "p1")
            run_practice1 "$THREADS" "$ITERATIONS"
            ;;
        "p2")
            run_practice2 "$THREADS" "$ITERATIONS"
            ;;
        "p3")
            run_practice3 "$THREADS" "$ITERATIONS"
            ;;
        "p4")
            run_practice4 "$THREADS" "$ITERATIONS"
            ;;
        "p5")
            run_practice5 "$THREADS" "$ITERATIONS"
            ;;
        "all")
            run_all_practices "$THREADS" "$ITERATIONS"
            ;;
        *)
            log "ERROR" "Práctica no reconocida: $PRACTICE"
            exit 1
            ;;
    esac
    
    # Generar análisis final si tenemos Python disponible
    if command -v python3 >/dev/null 2>&1 && [[ -f "${SCRIPTS_DIR}/bench.py" ]]; then
        log "INFO" "Generando análisis de datos con Python..."
        python3 "${SCRIPTS_DIR}/bench.py" --analyze "$RESULTS_FILE" || true
    fi
    
    # Mensaje final
    echo ""
    log "SUCCESS" "🎉 Ejecución del Lab06 completada exitosamente"
    echo ""
    echo "📊 Para análisis detallado:"
    echo "   python3 ${SCRIPTS_DIR}/bench.py --analyze ${RESULTS_FILE}"
    echo ""
    echo "📄 Para generar reporte:"
    echo "   python3 ${SCRIPTS_DIR}/bench.py --report ${DATA_DIR}"
    echo ""
    echo "🔍 Logs detallados en: $LOG_FILE"
    echo ""
}

# ============================================================================
# MANEJO DE SEÑALES Y CLEANUP
# ============================================================================

cleanup() {
    log "WARN" "Recibida señal de interrupción, limpiando..."
    
    # Terminar procesos hijos
    jobs -p | xargs -r kill 2>/dev/null || true
    
    # Guardar estado final
    echo "" >> "$LOG_FILE"
    echo "Execution interrupted at: $(date)" >> "$LOG_FILE"
    
    log "INFO" "Cleanup completado"
    exit 130
}

# Registrar manejadores de señales
trap cleanup SIGINT SIGTERM

# ============================================================================
# PUNTO DE ENTRADA
# ============================================================================

# Verificar que estamos en el directorio correcto
if [[ ! -f "Makefile" ]] || [[ ! -d "src" ]]; then
    echo -e "${RED}ERROR:${NC} Este script debe ejecutarse desde el directorio raíz del Lab06"
    echo "Estructura esperada:"
    echo "  Lab06/"
    echo "  ├── Makefile"
    echo "  ├── src/"
    echo "  ├── include/"
    echo "  ├── scripts/"
    echo "  └── ..."
    exit 1
fi

# Verificar que tenemos permisos de ejecución
if [[ ! -x "$0" ]]; then
    chmod +x "$0"
    log "INFO" "Permisos de ejecución otorgados a $0"
fi

# Ejecutar función principal con todos los argumentos
main "$@"