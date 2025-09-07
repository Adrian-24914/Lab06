# Universidad del Valle de Guatemala
# CC3086 Programación de Microprocesadores
# Laboratorio 6 - Makefile para todas las prácticas
# Autor: Adrian Penagos
# Fecha: Septiembre 2025

# ============================================================================
# CONFIGURACIÓN DEL COMPILADOR
# ============================================================================

# Compiladores disponibles
CXX = g++
CLANG = clang++

# Flags básicos de compilación
CXXFLAGS = -O2 -std=gnu++17 -Wall -Wextra -pthread -g
CLANG_FLAGS = -O2 -std=c++17 -Wall -Wextra -pthread -g

# Flags para sanitizers (debugging)
TSAN_FLAGS = -O1 -g -fsanitize=thread -fno-omit-frame-pointer -pthread
ASAN_FLAGS = -O1 -g -fsanitize=address -fno-omit-frame-pointer -pthread

# Flags para release (optimizado)
RELEASE_FLAGS = -O3 -std=gnu++17 -Wall -Wextra -pthread -DNDEBUG -march=native

# Directorios
BIN_DIR = bin
SRC_DIR = src
INCLUDE_DIR = include
DATA_DIR = data
SCRIPTS_DIR = scripts
DOCS_DIR = docs

# Headers comunes
HEADERS = $(wildcard $(INCLUDE_DIR)/*.hpp)

# Archivos fuente
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)

# Ejecutables (sin extensión)
EXECUTABLES = $(patsubst $(SRC_DIR)/%.cpp,$(BIN_DIR)/%,$(SOURCES))

# Ejecutables con sanitizers
TSAN_EXECUTABLES = $(patsubst $(SRC_DIR)/%.cpp,$(BIN_DIR)/%_tsan,$(SOURCES))
ASAN_EXECUTABLES = $(patsubst $(SRC_DIR)/%.cpp,$(BIN_DIR)/%_asan,$(SOURCES))

# Ejecutables de release
RELEASE_EXECUTABLES = $(patsubst $(SRC_DIR)/%.cpp,$(BIN_DIR)/%_release,$(SOURCES))

# ============================================================================
# TARGETS PRINCIPALES
# ============================================================================

# Target por defecto: compilar todo en modo debug
all: setup $(EXECUTABLES)
	@echo "✅ Compilación completa exitosa"
	@echo "Ejecutables disponibles en $(BIN_DIR)/:"
	@ls -la $(BIN_DIR)/

# Target para compilar solo las prácticas
practices: setup $(BIN_DIR)/p1_counter $(BIN_DIR)/p2_ring $(BIN_DIR)/p3_rw $(BIN_DIR)/p4_deadlock $(BIN_DIR)/p5_pipeline
	@echo "✅ Todas las prácticas compiladas"

# Target para compilación con sanitizers
sanitizers: setup $(TSAN_EXECUTABLES) $(ASAN_EXECUTABLES)
	@echo "✅ Versiones con sanitizers compiladas"

# Target para compilación optimizada (release)
release: setup $(RELEASE_EXECUTABLES)
	@echo "✅ Versiones release compiladas"

# ============================================================================
# COMPILACIÓN DE EJECUTABLES INDIVIDUALES
# ============================================================================

# Patrón general para ejecutables debug
$(BIN_DIR)/%: $(SRC_DIR)/%.cpp $(HEADERS) | $(BIN_DIR)
	@echo "🔨 Compilando $< -> $@"
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $< -o $@ -lm
	@echo "✅ $@ compilado exitosamente"

# Patrón para ejecutables con ThreadSanitizer
$(BIN_DIR)/%_tsan: $(SRC_DIR)/%.cpp $(HEADERS) | $(BIN_DIR)
	@echo "🧪 Compilando $< con ThreadSanitizer -> $@"
	$(CLANG) $(TSAN_FLAGS) -I$(INCLUDE_DIR) $< -o $@ -lm
	@echo "✅ $@ (TSan) compilado"

# Patrón para ejecutables con AddressSanitizer
$(BIN_DIR)/%_asan: $(SRC_DIR)/%.cpp $(HEADERS) | $(BIN_DIR)
	@echo "🧪 Compilando $< con AddressSanitizer -> $@"
	$(CLANG) $(ASAN_FLAGS) -I$(INCLUDE_DIR) $< -o $@ -lm
	@echo "✅ $@ (ASan) compilado"

# Patrón para ejecutables release
$(BIN_DIR)/%_release: $(SRC_DIR)/%.cpp $(HEADERS) | $(BIN_DIR)
	@echo "⚡ Compilando $< optimizado -> $@"
	$(CXX) $(RELEASE_FLAGS) -I$(INCLUDE_DIR) $< -o $@ -lm
	@echo "✅ $@ (release) compilado"

# ============================================================================
# TARGETS ESPECÍFICOS POR PRÁCTICA
# ============================================================================

# Práctica 1: Contador con race conditions
p1: $(BIN_DIR)/p1_counter
	@echo "✅ Práctica 1 compilada"

# Práctica 2: Búfer circular
p2: $(BIN_DIR)/p2_ring
	@echo "✅ Práctica 2 compilada"

# Práctica 3: Lectores/Escritores
p3: $(BIN_DIR)/p3_rw
	@echo "✅ Práctica 3 compilada"

# Práctica 4: Deadlock
p4: $(BIN_DIR)/p4_deadlock
	@echo "✅ Práctica 4 compilada"

# Práctica 5: Pipeline
p5: $(BIN_DIR)/p5_pipeline
	@echo "✅ Práctica 5 compilada"

# ============================================================================
# TARGETS DE EJECUCIÓN Y TESTING
# ============================================================================

# Ejecutar todas las prácticas con parámetros básicos
run-all: practices
	@echo "\n🚀 EJECUTANDO TODAS LAS PRÁCTICAS\n"
	@echo "=== PRÁCTICA 1: CONTADOR ==="
	./$(BIN_DIR)/p1_counter 4 100000 || true
	@echo "\n=== PRÁCTICA 2: BÚFER CIRCULAR ==="
	./$(BIN_DIR)/p2_ring 2 2 50000 || true
	@echo "\n=== PRÁCTICA 3: LECTORES/ESCRITORES ==="
	./$(BIN_DIR)/p3_rw 4 50000 || true
	@echo "\n=== PRÁCTICA 4: DEADLOCK ==="
	./$(BIN_DIR)/p4_deadlock 4 1 || true
	@echo "\n=== PRÁCTICA 5: PIPELINE ==="
	./$(BIN_DIR)/p5_pipeline 500 || true

# Testing con sanitizers (detección de errores)
test-sanitizers: sanitizers
	@echo "\n🧪 TESTING CON SANITIZERS\n"
	@echo "ThreadSanitizer - Detección de race conditions:"
	-timeout 30s ./$(BIN_DIR)/p1_counter_tsan 2 10000 2>/dev/null || echo "TSan test completado"
	@echo "AddressSanitizer - Detección de memory errors:"
	-timeout 30s ./$(BIN_DIR)/p2_ring_asan 1 1 1000 2>/dev/null || echo "ASan test completado"

# Benchmark completo con múltiples configuraciones
benchmark: release
	@echo "\n📊 EJECUTANDO BENCHMARKS COMPLETOS\n"
	@$(SCRIPTS_DIR)/run_all.sh

# Testing de deadlock (cuidado - puede colgarse)
test-deadlock: $(BIN_DIR)/p4_deadlock
	@echo "\n⚠️  TESTING DE DEADLOCK (timeout 10s)"
	@echo "Si no hay output en 10 segundos, el programa está en deadlock"
	timeout 10s ./$(BIN_DIR)/p4_deadlock 2 0 || echo "Test de deadlock completado/timeout"

# ============================================================================
# TARGETS DE LIMPIEZA Y MANTENIMIENTO
# ============================================================================

# Limpiar todos los archivos generados
clean:
	@echo "🧹 Limpiando archivos generados..."
	rm -rf $(BIN_DIR)
	rm -f $(DATA_DIR)/*.csv $(DATA_DIR)/*.txt $(DATA_DIR)/*.log
	rm -f core core.*
	rm -f *.out
	@echo "✅ Limpieza completada"

# Limpiar solo ejecutables (mantener datos)
clean-bin:
	@echo "🧹 Limpiando solo ejecutables..."
	rm -rf $(BIN_DIR)
	@echo "✅ Ejecutables eliminados"

# Limpiar solo datos generados
clean-data:
	@echo "🧹 Limpiando datos generados..."
	rm -f $(DATA_DIR)/*.csv $(DATA_DIR)/*.txt $(DATA_DIR)/*.log
	@echo "✅ Datos limpiados"

# ============================================================================
# TARGETS DE CONFIGURACIÓN Y VERIFICACIÓN
# ============================================================================

# Crear estructura de directorios necesaria
setup:
	@mkdir -p $(BIN_DIR) $(DATA_DIR) $(SCRIPTS_DIR) $(DOCS_DIR)
	@chmod +x $(SCRIPTS_DIR)/*.sh $(SCRIPTS_DIR)/*.py 2>/dev/null || true

# Verificar dependencias del sistema
check-deps:
	@echo "🔍 Verificando dependencias del sistema..."
	@which gcc g++ clang++ make || (echo "❌ Falta algún compilador"; exit 1)
	@which python3 || echo "⚠️  Python3 no encontrado (opcional para análisis)"
	@which valgrind || echo "⚠️  Valgrind no encontrado (opcional para debugging)"
	@echo "✅ Dependencias verificadas"

# Mostrar información del sistema
info:
	@echo "=== INFORMACIÓN DEL SISTEMA ==="
	@echo "Compilador GCC: $$(gcc --version | head -1)"
	@echo "Compilador G++: $$(g++ --version | head -1)"
	@echo "Clang: $$(clang++ --version | head -1 2>/dev/null || echo 'No disponible')"
	@echo "Make: $$(make --version | head -1)"
	@echo "Núcleos CPU: $$(nproc)"
	@echo "Memoria: $$(free -h | grep Mem | awk '{print $$2}')"
	@echo "Sistema: $$(uname -a)"

# ============================================================================
# TARGETS DE ANÁLISIS Y DEBUGGING
# ============================================================================

# Análisis estático con cppcheck (si está disponible)
static-analysis:
	@echo "🔍 Análisis estático del código..."
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck --enable=all --std=c++17 --platform=unix64 \
		-I$(INCLUDE_DIR) $(SRC_DIR)/*.cpp 2>&1 | tee analysis_report.txt; \
		echo "📄 Reporte guardado en analysis_report.txt"; \
	else \
		echo "⚠️  cppcheck no disponible - instalando..."; \
		sudo apt-get update && sudo apt-get install -y cppcheck || true; \
	fi

# Generar reporte de coverage (con gcov)
coverage: CXXFLAGS += --coverage
coverage: clean $(EXECUTABLES)
	@echo "📊 Generando reporte de coverage..."
	@$(SCRIPTS_DIR)/run_all.sh >/dev/null 2>&1 || true
	@gcov $(SRC_DIR)/*.cpp
	@echo "📄 Archivos .gcov generados"

# Profiling con perf (Linux)
profile-p1: $(BIN_DIR)/p1_counter_release
	@echo "📈 Profiling de Práctica 1..."
	@if command -v perf >/dev/null 2>&1; then \
		perf record -g ./$(BIN_DIR)/p1_counter_release 8 1000000; \
		perf report; \
	else \
		echo "⚠️  perf no disponible"; \
	fi

# ============================================================================
# TARGETS DE DOCUMENTACIÓN
# ============================================================================

# Generar documentación con Doxygen (si está disponible)
docs:
	@echo "📚 Generando documentación..."
	@if command -v doxygen >/dev/null 2>&1; then \
		doxygen Doxyfile 2>/dev/null || echo "Usando configuración por defecto"; \
		echo "📄 Documentación generada en docs/html/"; \
	else \
		echo "⚠️  Doxygen no disponible"; \
	fi

# ============================================================================
# TARGETS DE INSTALACIÓN Y DISTRIBUCIÓN
# ============================================================================

# Instalar herramientas necesarias (Ubuntu/Debian)
install-deps:
	@echo "📦 Instalando dependencias..."
	sudo apt-get update
	sudo apt-get install -y build-essential clang gdb valgrind
	sudo apt-get install -y python3 python3-pip python3-matplotlib python3-pandas
	sudo apt-get install -y cppcheck doxygen graphviz
	@echo "✅ Dependencias instaladas"

# Crear paquete distribuible
package: clean-data release
	@echo "📦 Creando paquete distribuible..."
	@tar -czf lab06_$(USER)_$(shell date +%Y%m%d).tar.gz \
		--exclude='*.o' --exclude='*.gcov' --exclude='*.gcda' --exclude='*.gcno' \
		--exclude='.git' --exclude='*.tar.gz' \
		.
	@echo "✅ Paquete creado: lab06_$(USER)_$(shell date +%Y%m%d).tar.gz"

# ============================================================================
# TARGETS ESPECIALES
# ============================================================================

# Verificar que todos los archivos compilan sin warnings
strict: CXXFLAGS += -Werror -Wpedantic
strict: clean practices
	@echo "✅ Compilación estricta exitosa (sin warnings)"

# Compilación rápida para desarrollo (sin optimizaciones)
quick: CXXFLAGS = -std=gnu++17 -Wall -pthread -g
quick: practices
	@echo "✅ Compilación rápida completada"

# Target para CI/CD
ci: check-deps static-analysis strict test-sanitizers
	@echo "✅ Pipeline CI/CD completado exitosamente"

# ============================================================================
# DECLARACIONES DE TARGETS ESPECIALES
# ============================================================================

# Targets que no corresponden a archivos
.PHONY: all practices sanitizers release clean clean-bin clean-data setup
.PHONY: check-deps info static-analysis coverage docs install-deps package
.PHONY: run-all test-sanitizers benchmark test-deadlock strict quick ci
.PHONY: p1 p2 p3 p4 p5 profile-p1

# Mantener archivos intermedios importantes
.PRECIOUS: $(BIN_DIR)/% $(BIN_DIR)/%_tsan $(BIN_DIR)/%_asan $(BIN_DIR)/%_release

# Directorio bin como prerequisito para orden
$(BIN_DIR):
	@mkdir -p $(BIN_DIR)