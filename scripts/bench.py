#!/usr/bin/env python3
"""
Universidad del Valle de Guatemala
CC3086 Programación de Microprocesadores
Laboratorio 6 - Sistema de Benchmarking y Análisis

Autor: Adrian Penagos
Fecha: Septiembre 2025
Propósito: Ejecutar benchmarks automatizados, analizar resultados y generar reportes
"""

import os
import sys
import subprocess
import time
import json
import csv
import statistics
import argparse
import datetime
from pathlib import Path
from typing import List, Dict, Any, Tuple, Optional
from dataclasses import dataclass, asdict
import re

# Intentar importar librerías de análisis (opcionales)
try:
    import matplotlib.pyplot as plt
    import numpy as np
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("⚠️  matplotlib no disponible - gráficas deshabilitadas")

try:
    import pandas as pd
    HAS_PANDAS = True
except ImportError:
    HAS_PANDAS = False
    print("⚠️  pandas no disponible - análisis avanzado deshabilitado")

# ============================================================================
# CONFIGURACIÓN Y CONSTANTES
# ============================================================================

# Directorios del proyecto
BIN_DIR = Path("bin")
DATA_DIR = Path("data")
SCRIPTS_DIR = Path("scripts")
DOCS_DIR = Path("docs")

# Configuración de benchmarks
DEFAULT_REPETITIONS = 5
DEFAULT_TIMEOUT = 30
WARMUP_ITERATIONS = 1

# Archivos de resultados
RESULTS_FILE = DATA_DIR / "benchmark_results.csv"
ANALYSIS_FILE = DATA_DIR / "analysis_report.json"
REPORT_FILE = DATA_DIR / "performance_report.html"

# Configuraciones de test para cada práctica
PRACTICE_CONFIGS = {
    'p1_counter': {
        'executable': 'p1_counter',
        'name': 'Race Conditions en Contador',
        'params': [
            {'threads': 1, 'iterations': 100000},
            {'threads': 2, 'iterations': 100000},
            {'threads': 4, 'iterations': 100000},
            {'threads': 8, 'iterations': 100000},
        ],
        'metrics': ['time', 'throughput', 'correctness'],
        'timeout': 15
    },
    'p2_ring': {
        'executable': 'p2_ring',
        'name': 'Búfer Circular Productor-Consumidor',
        'params': [
            {'producers': 1, 'consumers': 1, 'items': 50000},
            {'producers': 2, 'consumers': 1, 'items': 50000},
            {'producers': 1, 'consumers': 2, 'items': 50000},
            {'producers': 2, 'consumers': 2, 'items': 50000},
        ],
        'metrics': ['time', 'throughput', 'efficiency'],
        'timeout': 20
    },
    'p3_rw': {
        'executable': 'p3_rw',
        'name': 'Lectores/Escritores HashMap',
        'params': [
            {'threads': 2, 'operations': 50000},
            {'threads': 4, 'operations': 50000},
            {'threads': 8, 'operations': 50000},
        ],
        'metrics': ['time', 'throughput', 'read_write_ratio'],
        'timeout': 25
    },
    'p4_deadlock': {
        'executable': 'p4_deadlock',
        'name': 'Prevención de Deadlock',
        'params': [
            {'threads': 2, 'skip_demo': 1},
            {'threads': 4, 'skip_demo': 1},
            {'threads': 8, 'skip_demo': 1},
        ],
        'metrics': ['time', 'success_rate', 'retry_count'],
        'timeout': 20
    },
    'p5_pipeline': {
        'executable': 'p5_pipeline',
        'name': 'Pipeline con Barreras',
        'params': [
            {'ticks': 500},
            {'ticks': 1000},
            {'ticks': 2000},
        ],
        'metrics': ['time', 'throughput', 'latency', 'efficiency'],
        'timeout': 30
    }
}

# ============================================================================
# CLASES DE DATOS
# ============================================================================

@dataclass
class BenchmarkResult:
    """Resultado de una ejecución de benchmark"""
    practice: str
    config: str
    timestamp: str
    execution_time: float
    throughput: float
    success: bool
    stdout: str = ""
    stderr: str = ""
    metrics: Dict[str, Any] = None
    
    def __post_init__(self):
        if self.metrics is None:
            self.metrics = {}

@dataclass
class BenchmarkSuite:
    """Suite completa de benchmarks para una práctica"""
    practice: str
    name: str
    results: List[BenchmarkResult]
    statistics: Dict[str, Any] = None
    
    def __post_init__(self):
        if self.statistics is None:
            self.statistics = {}

# ============================================================================
# CLASE PRINCIPAL DE BENCHMARKING
# ============================================================================

class Lab06Benchmarker:
    """Sistema de benchmarking para el Laboratorio 6"""
    
    def __init__(self, repetitions: int = DEFAULT_REPETITIONS, timeout: int = DEFAULT_TIMEOUT):
        self.repetitions = repetitions
        self.timeout = timeout
        self.results: List[BenchmarkResult] = []
        self.suites: Dict[str, BenchmarkSuite] = {}
        
        # Crear directorios necesarios
        DATA_DIR.mkdir(exist_ok=True)
        
        # Configurar logging
        self.log_file = DATA_DIR / f"benchmark_log_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"
    
    def log(self, level: str, message: str):
        """Logging con timestamp"""
        timestamp = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        log_entry = f"[{timestamp}] [{level}] {message}"
        
        # Escribir a archivo
        with open(self.log_file, 'a', encoding='utf-8') as f:
            f.write(log_entry + '\n')
        
        # Imprimir a consola con colores
        colors = {
            'INFO': '\033[0;36m',    # Cyan
            'WARN': '\033[1;33m',    # Yellow
            'ERROR': '\033[0;31m',   # Red
            'SUCCESS': '\033[0;32m', # Green
            'DEBUG': '\033[0;35m',   # Purple
        }
        
        color = colors.get(level, '')
        reset = '\033[0m'
        print(f"{color}[{level}]{reset} {message}")
    
    def check_executable(self, practice: str) -> bool:
        """Verificar que el ejecutable existe y es ejecutable"""
        config = PRACTICE_CONFIGS.get(practice)
        if not config:
            self.log("ERROR", f"Práctica desconocida: {practice}")
            return False
        
        executable_path = BIN_DIR / config['executable']
        
        if not executable_path.exists():
            self.log("ERROR", f"Ejecutable no encontrado: {executable_path}")
            self.log("INFO", "Ejecute 'make' para compilar primero")
            return False
        
        if not os.access(executable_path, os.X_OK):
            self.log("ERROR", f"Ejecutable sin permisos: {executable_path}")
            return False
        
        return True
    
    def run_single_benchmark(self, practice: str, params: Dict[str, Any]) -> BenchmarkResult:
        """Ejecutar un benchmark individual"""
        config = PRACTICE_CONFIGS[practice]
        executable_path = BIN_DIR / config['executable']
        
        # Construir comando
        cmd_parts = [str(executable_path)]
        
        # Agregar parámetros específicos por práctica
        if practice == 'p1_counter':
            cmd_parts.extend([str(params['threads']), str(params['iterations'])])
        elif practice == 'p2_ring':
            cmd_parts.extend([str(params['producers']), str(params['consumers']), str(params['items'])])
        elif practice == 'p3_rw':
            cmd_parts.extend([str(params['threads']), str(params['operations'])])
        elif practice == 'p4_deadlock':
            cmd_parts.extend([str(params['threads']), str(params.get('skip_demo', 1))])
        elif practice == 'p5_pipeline':
            cmd_parts.append(str(params['ticks']))
        
        cmd = ' '.join(cmd_parts)
        config_str = '_'.join(f"{k}={v}" for k, v in params.items())
        
        self.log("INFO", f"Ejecutando: {cmd}")
        
        try:
            # Ejecutar comando con timeout
            start_time = time.time()
            process = subprocess.run(
                cmd_parts,
                timeout=config['timeout'],
                capture_output=True,
                text=True,
                cwd='.'
            )
            end_time = time.time()
            
            execution_time = end_time - start_time
            success = process.returncode == 0
            
            # Extraer métricas del output
            metrics = self.parse_output(practice, process.stdout, process.stderr)
            
            # Calcular throughput
            throughput = self.calculate_throughput(practice, params, execution_time, metrics)
            
            result = BenchmarkResult(
                practice=practice,
                config=config_str,
                timestamp=datetime.datetime.now().isoformat(),
                execution_time=execution_time,
                throughput=throughput,
                success=success,
                stdout=process.stdout,
                stderr=process.stderr,
                metrics=metrics
            )
            
            if success:
                self.log("SUCCESS", f"Benchmark completado en {execution_time:.3f}s (throughput: {throughput:.2f} ops/s)")
            else:
                self.log("WARN", f"Benchmark falló con código {process.returncode}")
            
            return result
            
        except subprocess.TimeoutExpired:
            self.log("WARN", f"Benchmark timeout después de {config['timeout']}s")
            return BenchmarkResult(
                practice=practice,
                config=config_str,
                timestamp=datetime.datetime.now().isoformat(),
                execution_time=config['timeout'],
                throughput=0.0,
                success=False,
                stderr="Timeout expired"
            )
        
        except Exception as e:
            self.log("ERROR", f"Error ejecutando benchmark: {e}")
            return BenchmarkResult(
                practice=practice,
                config=config_str,
                timestamp=datetime.datetime.now().isoformat(),
                execution_time=0.0,
                throughput=0.0,
                success=False,
                stderr=str(e)
            )
    
    def parse_output(self, practice: str, stdout: str, stderr: str) -> Dict[str, Any]:
        """Parsear output del programa para extraer métricas"""
        metrics = {}
        
        try:
            # Patrones comunes de extracción
            patterns = {
                'time': r'Tiempo[:\s]+([0-9.]+)',
                'throughput': r'Throughput[:\s]+([0-9.]+)',
                'operations': r'operaciones[:\s]+([0-9]+)',
                'items_produced': r'producidos[:\s]+([0-9]+)',
                'items_consumed': r'consumidos[:\s]+([0-9]+)',
                'speedup': r'Speedup[:\s]+([0-9.]+)',
                'latency': r'latencia[:\s]+([0-9.]+)',
                'efficiency': r'eficiencia[:\s]+([0-9.]+)',
            }
            
            combined_output = stdout + '\n' + stderr
            
            for metric_name, pattern in patterns.items():
                matches = re.findall(pattern, combined_output, re.IGNORECASE)
                if matches:
                    try:
                        # Tomar el último valor encontrado
                        value = float(matches[-1])
                        metrics[metric_name] = value
                    except ValueError:
                        pass
            
            # Métricas específicas por práctica
            if practice == 'p1_counter':
                # Buscar resultado vs esperado
                result_match = re.search(r'Resultado:\s*(\d+).*esperado:\s*(\d+)', combined_output)
                if result_match:
                    actual, expected = map(int, result_match.groups())
                    metrics['correctness'] = 1.0 if actual == expected else actual / expected
            
            elif practice == 'p2_ring':
                # Buscar estadísticas de productor/consumidor
                prod_match = re.search(r'producidos:\s*(\d+)', combined_output)
                cons_match = re.search(r'consumidos:\s*(\d+)', combined_output)
                if prod_match and cons_match:
                    produced = int(prod_match.group(1))
                    consumed = int(cons_match.group(1))
                    if produced > 0:
                        metrics['efficiency'] = consumed / produced
            
            elif practice == 'p3_rw':
                # Buscar proporción de lecturas/escrituras
                read_match = re.search(r'R:\s*(\d+)', combined_output)
                write_match = re.search(r'W:\s*(\d+)', combined_output)
                if read_match and write_match:
                    reads = int(read_match.group(1))
                    writes = int(write_match.group(1))
                    total = reads + writes
                    if total > 0:
                        metrics['read_ratio'] = reads / total
                        metrics['write_ratio'] = writes / total
            
            elif practice == 'p5_pipeline':
                # Buscar estadísticas del pipeline
                filtered_match = re.search(r'filtrados:\s*(\d+)', combined_output)
                generated_match = re.search(r'generados:\s*(\d+)', combined_output)
                if filtered_match and generated_match:
                    filtered = int(filtered_match.group(1))
                    generated = int(generated_match.group(1))
                    if generated > 0:
                        metrics['filter_efficiency'] = filtered / generated
        
        except Exception as e:
            self.log("DEBUG", f"Error parseando output: {e}")
        
        return metrics
    
    def calculate_throughput(self, practice: str, params: Dict[str, Any], 
                           execution_time: float, metrics: Dict[str, Any]) -> float:
        """Calcular throughput en operaciones por segundo"""
        if execution_time <= 0:
            return 0.0
        
        try:
            if practice == 'p1_counter':
                total_ops = params['threads'] * params['iterations']
                return total_ops / execution_time
            
            elif practice == 'p2_ring':
                total_items = params['producers'] * params['items']
                return total_items / execution_time
            
            elif practice == 'p3_rw':
                total_ops = params['threads'] * params['operations']
                return total_ops / execution_time
            
            elif practice == 'p4_deadlock':
                # Para deadlock, throughput no es tan relevante
                return 1.0 / execution_time if execution_time > 0 else 0.0
            
            elif practice == 'p5_pipeline':
                # Para pipeline, usar ticks procesados
                return params['ticks'] / execution_time
            
        except Exception as e:
            self.log("DEBUG", f"Error calculando throughput: {e}")
        
        return 0.0
    
    def run_practice_suite(self, practice: str) -> BenchmarkSuite:
        """Ejecutar suite completa de benchmarks para una práctica"""
        if practice not in PRACTICE_CONFIGS:
            raise ValueError(f"Práctica desconocida: {practice}")
        
        if not self.check_executable(practice):
            raise RuntimeError(f"Ejecutable no disponible para práctica {practice}")
        
        config = PRACTICE_CONFIGS[practice]
        self.log("INFO", f"Iniciando suite de benchmarks: {config['name']}")
        
        suite_results = []
        
        for param_set in config['params']:
            self.log("INFO", f"Configuración: {param_set}")
            
            # Ejecutar múltiples repeticiones
            repetition_results = []
            
            for rep in range(self.repetitions):
                self.log("DEBUG", f"Repetición {rep + 1}/{self.repetitions}")
                
                result = self.run_single_benchmark(practice, param_set)
                repetition_results.append(result)
                suite_results.append(result)
                
                # Pausa entre repeticiones
                if rep < self.repetitions - 1:
                    time.sleep(0.5)
            
            # Calcular estadísticas para esta configuración
            successful_results = [r for r in repetition_results if r.success]
            if successful_results:
                times = [r.execution_time for r in successful_results]
                throughputs = [r.throughput for r in successful_results]
                
                self.log("SUCCESS", 
                    f"Config completada - Tiempo promedio: {statistics.mean(times):.3f}s ± {statistics.stdev(times) if len(times) > 1 else 0:.3f}s")
        
        # Crear suite con estadísticas
        suite = BenchmarkSuite(
            practice=practice,
            name=config['name'],
            results=suite_results
        )
        
        suite.statistics = self.calculate_suite_statistics(suite)
        
        self.suites[practice] = suite
        self.log("SUCCESS", f"Suite {config['name']} completada con {len(suite_results)} resultados")
        
        return suite
    
    def calculate_suite_statistics(self, suite: BenchmarkSuite) -> Dict[str, Any]:
        """Calcular estadísticas para una suite completa"""
        successful_results = [r for r in suite.results if r.success]
        
        if not successful_results:
            return {'success_rate': 0.0, 'total_runs': len(suite.results)}
        
        times = [r.execution_time for r in successful_results]
        throughputs = [r.throughput for r in successful_results]
        
        stats = {
            'success_rate': len(successful_results) / len(suite.results),
            'total_runs': len(suite.results),
            'successful_runs': len(successful_results),
            'time_stats': {
                'mean': statistics.mean(times),
                'median': statistics.median(times),
                'stdev': statistics.stdev(times) if len(times) > 1 else 0.0,
                'min': min(times),
                'max': max(times)
            },
            'throughput_stats': {
                'mean': statistics.mean(throughputs),
                'median': statistics.median(throughputs),
                'stdev': statistics.stdev(throughputs) if len(throughputs) > 1 else 0.0,
                'min': min(throughputs),
                'max': max(throughputs)
            }
        }
        
        return stats
    
    def save_results_csv(self, filename: Path = RESULTS_FILE):
        """Guardar resultados en formato CSV"""
        filename.parent.mkdir(exist_ok=True)
        
        # Verificar si el archivo existe para agregar header
        file_exists = filename.exists()
        
        with open(filename, 'a', newline='', encoding='utf-8') as csvfile:
            fieldnames = [
                'practice', 'config', 'timestamp', 'execution_time', 
                'throughput', 'success', 'additional_metrics'
            ]
            
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            
            # Escribir header solo si es archivo nuevo
            if not file_exists:
                writer.writeheader()
            
            # Escribir resultados
            for result in self.results:
                writer.writerow({
                    'practice': result.practice,
                    'config': result.config,
                    'timestamp': result.timestamp,
                    'execution_time': result.execution_time,
                    'throughput': result.throughput,
                    'success': result.success,
                    'additional_metrics': json.dumps(result.metrics)
                })
        
        self.log("SUCCESS", f"Resultados guardados en {filename}")
    
    def save_analysis_json(self, filename: Path = ANALYSIS_FILE):
        """Guardar análisis completo en formato JSON"""
        analysis_data = {
            'timestamp': datetime.datetime.now().isoformat(),
            'configuration': {
                'repetitions': self.repetitions,
                'timeout': self.timeout
            },
            'suites': {}
        }
        
        for practice, suite in self.suites.items():
            analysis_data['suites'][practice] = {
                'name': suite.name,
                'statistics': suite.statistics,
                'results_count': len(suite.results)
            }
        
        filename.parent.mkdir(exist_ok=True)
        with open(filename, 'w', encoding='utf-8') as f:
            json.dump(analysis_data, f, indent=2, ensure_ascii=False)
        
        self.log("SUCCESS", f"Análisis guardado en {filename}")
    
    def generate_plots(self):
        """Generar gráficas de rendimiento"""
        if not HAS_MATPLOTLIB:
            self.log("WARN", "matplotlib no disponible - omitiendo gráficas")
            return
        
        self.log("INFO", "Generando gráficas de rendimiento...")
        
        plots_dir = DATA_DIR / "plots"
        plots_dir.mkdir(exist_ok=True)
        
        for practice, suite in self.suites.items():
            try:
                self.create_practice_plots(suite, plots_dir)
            except Exception as e:
                self.log("ERROR", f"Error generando gráficas para {practice}: {e}")
        
        # Gráfica comparativa general
        try:
            self.create_comparison_plots(plots_dir)
        except Exception as e:
            self.log("ERROR", f"Error generando gráficas comparativas: {e}")
    
    def create_practice_plots(self, suite: BenchmarkSuite, plots_dir: Path):
        """Crear gráficas específicas para una práctica"""
        practice = suite.practice
        successful_results = [r for r in suite.results if r.success]
        
        if not successful_results:
            self.log("WARN", f"No hay resultados exitosos para {practice}")
            return
        
        # Agrupar resultados por configuración
        config_groups = {}
        for result in successful_results:
            config = result.config
            if config not in config_groups:
                config_groups[config] = []
            config_groups[config].append(result)
        
        # Gráfica de tiempo de ejecución
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 6))
        
        configs = list(config_groups.keys())
        times_mean = []
        times_std = []
        throughput_mean = []
        throughput_std = []
        
        for config in configs:
            results = config_groups[config]
            times = [r.execution_time for r in results]
            throughputs = [r.throughput for r in results]
            
            times_mean.append(statistics.mean(times))
            times_std.append(statistics.stdev(times) if len(times) > 1 else 0)
            throughput_mean.append(statistics.mean(throughputs))
            throughput_std.append(statistics.stdev(throughputs) if len(throughputs) > 1 else 0)
        
        # Plot tiempo de ejecución
        x_pos = range(len(configs))
        ax1.bar(x_pos, times_mean, yerr=times_std, capsize=5, alpha=0.7)
        ax1.set_xlabel('Configuración')
        ax1.set_ylabel('Tiempo (segundos)')
        ax1.set_title(f'{suite.name} - Tiempo de Ejecución')
        ax1.set_xticks(x_pos)
        ax1.set_xticklabels(configs, rotation=45, ha='right')
        ax1.grid(True, alpha=0.3)
        
        # Plot throughput
        ax2.bar(x_pos, throughput_mean, yerr=throughput_std, capsize=5, alpha=0.7, color='orange')
        ax2.set_xlabel('Configuración')
        ax2.set_ylabel('Throughput (ops/seg)')
        ax2.set_title(f'{suite.name} - Throughput')
        ax2.set_xticks(x_pos)
        ax2.set_xticklabels(configs, rotation=45, ha='right')
        ax2.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(plots_dir / f"{practice}_performance.png", dpi=300, bbox_inches='tight')
        plt.close()
        
        self.log("SUCCESS", f"Gráficas guardadas para {practice}")
    
    def create_comparison_plots(self, plots_dir: Path):
        """Crear gráficas comparativas entre prácticas"""
        if len(self.suites) < 2:
            self.log("INFO", "Necesarias al menos 2 prácticas para comparación")
            return
        
        fig, ax = plt.subplots(figsize=(12, 8))
        
        practices = []
        mean_times = []
        std_times = []
        
        for practice, suite in self.suites.items():
            successful_results = [r for r in suite.results if r.success]
            if successful_results:
                times = [r.execution_time for r in successful_results]
                practices.append(PRACTICE_CONFIGS[practice]['name'])
                mean_times.append(statistics.mean(times))
                std_times.append(statistics.stdev(times) if len(times) > 1 else 0)
        
        if practices:
            x_pos = range(len(practices))
            bars = ax.bar(x_pos, mean_times, yerr=std_times, capsize=5, 
                         alpha=0.7, color=['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd'][:len(practices)])
            
            ax.set_xlabel('Práctica')
            ax.set_ylabel('Tiempo Promedio (segundos)')
            ax.set_title('Comparación de Rendimiento entre Prácticas')
            ax.set_xticks(x_pos)
            ax.set_xticklabels(practices, rotation=45, ha='right')
            ax.grid(True, alpha=0.3)
            
            # Añadir valores en las barras
            for i, (bar, time, std) in enumerate(zip(bars, mean_times, std_times)):
                height = bar.get_height()
                ax.text(bar.get_x() + bar.get_width()/2., height + std + 0.01,
                       f'{time:.3f}s', ha='center', va='bottom', fontsize=10)
            
            plt.tight_layout()
            plt.savefig(plots_dir / "practices_comparison.png", dpi=300, bbox_inches='tight')
            plt.close()
            
            self.log("SUCCESS", "Gráfica comparativa guardada")

# ============================================================================
# FUNCIONES DE ANÁLISIS Y REPORTES
# ============================================================================

def analyze_csv_results(csv_file: Path):
    """Analizar resultados desde archivo CSV"""
    if not csv_file.exists():
        print(f"❌ Archivo no encontrado: {csv_file}")
        return
    
    print(f"📊 Analizando resultados desde {csv_file}")
    
    if HAS_PANDAS:
        analyze_with_pandas(csv_file)
    else:
        analyze_with_builtin(csv_file)

def analyze_with_pandas(csv_file: Path):
    """Análisis avanzado con pandas"""
    try:
        df = pd.read_csv(csv_file)
        
        print(f"\n=== RESUMEN GENERAL ===")
        print(f"Total de experimentos: {len(df)}")
        print(f"Prácticas analizadas: {', '.join(df['practice'].unique())}")
        print(f"Tasa de éxito: {df['success'].mean():.1%}")
        
        # Análisis por práctica
        print(f"\n=== ANÁLISIS POR PRÁCTICA ===")
        for practice in df['practice'].unique():
            practice_df = df[df['practice'] == practice]
            successful_df = practice_df[practice_df['success'] == True]
            
            if len(successful_df) > 0:
                print(f"\n{practice.upper()}:")
                print(f"  Éxito: {len(successful_df)}/{len(practice_df)} ({len(successful_df)/len(practice_df):.1%})")
                print(f"  Tiempo promedio: {successful_df['execution_time'].mean():.3f}s ± {successful_df['execution_time'].std():.3f}s")
                print(f"  Throughput promedio: {successful_df['throughput'].mean():.2f} ops/s")
                print(f"  Rango throughput: {successful_df['throughput'].min():.2f} - {successful_df['throughput'].max():.2f}")
        
        # Top configuraciones por throughput
        print(f"\n=== TOP CONFIGURACIONES (THROUGHPUT) ===")
        top_configs = df[df['success'] == True].nlargest(5, 'throughput')
        for idx, row in top_configs.iterrows():
            print(f"  {row['practice']} ({row['config']}): {row['throughput']:.2f} ops/s")
        
        # Crear gráfica si matplotlib disponible
        if HAS_MATPLOTLIB:
            create_pandas_plots(df)
        
    except Exception as e:
        print(f"❌ Error en análisis con pandas: {e}")

def analyze_with_builtin(csv_file: Path):
    """Análisis básico con librerías estándar"""
    try:
        results_by_practice = {}
        
        with open(csv_file, 'r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            
            for row in reader:
                practice = row['practice']
                if practice not in results_by_practice:
                    results_by_practice[practice] = []
                
                results_by_practice[practice].append({
                    'config': row['config'],
                    'time': float(row['execution_time']),
                    'throughput': float(row['throughput']),
                    'success': row['success'].lower() == 'true'
                })
        
        print(f"\n=== ANÁLISIS BÁSICO ===")
        for practice, results in results_by_practice.items():
            successful = [r for r in results if r['success']]
            
            if successful:
                times = [r['time'] for r in successful]
                throughputs = [r['throughput'] for r in successful]
                
                print(f"\n{practice.upper()}:")
                print(f"  Experimentos exitosos: {len(successful)}/{len(results)}")
                print(f"  Tiempo promedio: {statistics.mean(times):.3f}s")
                print(f"  Throughput promedio: {statistics.mean(throughputs):.2f} ops/s")
                print(f"  Mejor throughput: {max(throughputs):.2f} ops/s")
    
    except Exception as e:
        print(f"❌ Error en análisis básico: {e}")

def create_pandas_plots(df):
    """Crear gráficas con pandas y matplotlib"""
    try:
        plots_dir = DATA_DIR / "plots"
        plots_dir.mkdir(exist_ok=True)
        
        # Gráfica de throughput por práctica
        successful_df = df[df['success'] == True]
        
        if len(successful_df) > 0:
            fig, ax = plt.subplots(figsize=(12, 6))
            
            practices = successful_df['practice'].unique()
            throughput_data = [successful_df[successful_df['practice'] == p]['throughput'].values 
                             for p in practices]
            
            ax.boxplot(throughput_data, labels=practices)
            ax.set_ylabel('Throughput (ops/seg)')
            ax.set_title('Distribución de Throughput por Práctica')
            ax.grid(True, alpha=0.3)
            
            plt.xticks(rotation=45, ha='right')
            plt.tight_layout()
            plt.savefig(plots_dir / "throughput_distribution.png", dpi=300, bbox_inches='tight')
            plt.close()
            
            print(f"📈 Gráficas guardadas en {plots_dir}")
    
    except Exception as e:
        print(f"❌ Error generando gráficas: {e}")

def generate_html_report(data_dir: Path):
    """Generar reporte HTML completo"""
    try:
        results_file = data_dir / "benchmark_results.csv"
        analysis_file = data_dir / "analysis_report.json"
        report_file = data_dir / "performance_report.html"
        
        html_content = f"""
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Lab06 - Reporte de Rendimiento</title>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 40px; background-color: #f5f5f5; }}
        .container {{ max-width: 1200px; margin: 0 auto; background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }}
        h1, h2 {{ color: #2c3e50; }}
        .header {{ text-align: center; margin-bottom: 40px; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; border-radius: 8px; }}
        .summary {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin: 30px 0; }}
        .card {{ background: #f8f9fa; padding: 20px; border-radius: 8px; border-left: 4px solid #007bff; }}
        .metric {{ font-size: 2em; font-weight: bold; color: #007bff; }}
        table {{ width: 100%; border-collapse: collapse; margin: 20px 0; }}
        th, td {{ padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }}
        th {{ background-color: #f1f1f1; font-weight: bold; }}
        .success {{ color: #28a745; }}
        .warning {{ color: #ffc107; }}
        .error {{ color: #dc3545; }}
        .plots {{ text-align: center; margin: 30px 0; }}
        .plot {{ margin: 20px 0; }}
        img {{ max-width: 100%; height: auto; border-radius: 8px; box-shadow: 0 2px 8px rgba(0,0,0,0.1); }}
        .footer {{ text-align: center; margin-top: 40px; padding-top: 20px; border-top: 1px solid #ddd; color: #666; }}
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🧵 Laboratorio 6 - Reporte de Rendimiento</h1>
            <p>Universidad del Valle de Guatemala - CC3086</p>
            <p>Generado: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>
        </div>
        
        <h2>📊 Resumen Ejecutivo</h2>
        <div class="summary">
            <div class="card">
                <h3>Experimentos Totales</h3>
                <div class="metric" id="total-experiments">-</div>
            </div>
            <div class="card">
                <h3>Tasa de Éxito</h3>
                <div class="metric" id="success-rate">-</div>
            </div>
            <div class="card">
                <h3>Prácticas Evaluadas</h3>
                <div class="metric" id="practices-count">-</div>
            </div>
        </div>
        
        <h2>📈 Gráficas de Rendimiento</h2>
        <div class="plots">
            <div class="plot">
                <h3>Comparación General</h3>
                <img src="plots/practices_comparison.png" alt="Comparación entre prácticas" onerror="this.style.display='none'">
            </div>
            <div class="plot">
                <h3>Distribución de Throughput</h3>
                <img src="plots/throughput_distribution.png" alt="Distribución de throughput" onerror="this.style.display='none'">
            </div>
        </div>
        
        <h2>🔍 Análisis Detallado por Práctica</h2>
        <div id="detailed-analysis">
            <!-- Se llenará dinámicamente -->
        </div>
        
        <div class="footer">
            <p>Reporte generado automáticamente por el sistema de benchmarking del Lab06</p>
            <p>Para más detalles, consulte los archivos CSV y de log en el directorio data/</p>
        </div>
    </div>
    
    <script>
        // JavaScript para cargar datos dinámicamente si están disponibles
        document.addEventListener('DOMContentLoaded', function() {{
            // Aquí se podría cargar datos desde JSON si está disponible
            console.log('Reporte Lab06 cargado');
        }});
    </script>
</body>
</html>
        """
        
        with open(report_file, 'w', encoding='utf-8') as f:
            f.write(html_content)
        
        print(f"📄 Reporte HTML generado: {report_file}")
        
    except Exception as e:
        print(f"❌ Error generando reporte HTML: {e}")

# ============================================================================
# FUNCIÓN PRINCIPAL Y CLI
# ============================================================================

def main():
    """Función principal del script"""
    parser = argparse.ArgumentParser(
        description="Sistema de Benchmarking para Lab06",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Ejemplos de uso:
  python3 bench.py p1_counter 4 100000 5          # Benchmark práctica 1
  python3 bench.py --all                           # Todas las prácticas
  python3 bench.py --analyze data/results.csv     # Analizar resultados
  python3 bench.py --report data/                  # Generar reporte HTML
  python3 bench.py --quick p3_rw                  # Test rápido práctica 3
        """
    )
    
    parser.add_argument('practice', nargs='?', 
                       choices=['p1_counter', 'p2_ring', 'p3_rw', 'p4_deadlock', 'p5_pipeline'],
                       help='Práctica a benchmarker')
    parser.add_argument('params', nargs='*', help='Parámetros específicos de la práctica')
    parser.add_argument('repetitions', nargs='?', type=int, default=DEFAULT_REPETITIONS,
                       help=f'Número de repeticiones (default: {DEFAULT_REPETITIONS})')
    
    parser.add_argument('--all', action='store_true', help='Ejecutar todas las prácticas')
    parser.add_argument('--analyze', type=Path, help='Analizar archivo CSV de resultados')
    parser.add_argument('--report', type=Path, help='Generar reporte HTML desde directorio')
    parser.add_argument('--quick', action='store_true', help='Modo rápido (menos repeticiones)')
    parser.add_argument('--timeout', type=int, default=DEFAULT_TIMEOUT, help='Timeout en segundos')
    parser.add_argument('--output', type=Path, help='Archivo de salida para resultados')
    parser.add_argument('--plots', action='store_true', help='Generar solo gráficas')
    parser.add_argument('--verbose', '-v', action='store_true', help='Output detallado')
    
    args = parser.parse_args()
    
    # Configurar repeticiones para modo rápido
    if args.quick:
        repetitions = 2
        timeout = 15
    else:
        repetitions = args.repetitions
        timeout = args.timeout
    
    # Funciones especiales
    if args.analyze:
        analyze_csv_results(args.analyze)
        return
    
    if args.report:
        generate_html_report(args.report)
        return
    
    if args.plots and args.analyze:
        if HAS_MATPLOTLIB and HAS_PANDAS:
            df = pd.read_csv(args.analyze)
            create_pandas_plots(df)
        else:
            print("❌ Se requieren matplotlib y pandas para generar gráficas")
        return
    
    # Verificar que estamos en el directorio correcto
    if not Path("Makefile").exists() or not Path("src").exists():
        print("❌ Este script debe ejecutarse desde el directorio raíz del Lab06")
        sys.exit(1)
    
    # Crear benchmarker
    benchmarker = Lab06Benchmarker(repetitions=repetitions, timeout=timeout)
    
    try:
        if args.all:
            # Ejecutar todas las prácticas
            benchmarker.log("INFO", "Iniciando benchmark completo de todas las prácticas")
            
            for practice in PRACTICE_CONFIGS.keys():
                try:
                    benchmarker.run_practice_suite(practice)
                    benchmarker.results.extend(benchmarker.suites[practice].results)
                except Exception as e:
                    benchmarker.log("ERROR", f"Error en práctica {practice}: {e}")
                    continue
        
        elif args.practice:
            # Ejecutar práctica específica
            if args.params:
                # Ejecutar con parámetros específicos
                practice = args.practice
                
                # Parsear parámetros según la práctica
                params = {}
                if practice == 'p1_counter' and len(args.params) >= 2:
                    params = {'threads': int(args.params[0]), 'iterations': int(args.params[1])}
                elif practice == 'p2_ring' and len(args.params) >= 3:
                    params = {'producers': int(args.params[0]), 'consumers': int(args.params[1]), 'items': int(args.params[2])}
                elif practice == 'p3_rw' and len(args.params) >= 2:
                    params = {'threads': int(args.params[0]), 'operations': int(args.params[1])}
                elif practice == 'p4_deadlock' and len(args.params) >= 1:
                    params = {'threads': int(args.params[0]), 'skip_demo': 1}
                elif practice == 'p5_pipeline' and len(args.params) >= 1:
                    params = {'ticks': int(args.params[0])}
                else:
                    benchmarker.log("ERROR", f"Parámetros insuficientes para {practice}")
                    sys.exit(1)
                
                # Ejecutar benchmark individual múltiples veces
                for i in range(repetitions):
                    result = benchmarker.run_single_benchmark(practice, params)
                    benchmarker.results.append(result)
                
            else:
                # Ejecutar suite completa
                suite = benchmarker.run_practice_suite(args.practice)
                benchmarker.results.extend(suite.results)
        
        else:
            parser.print_help()
            return
        
        # Guardar resultados
        output_file = args.output or RESULTS_FILE
        benchmarker.save_results_csv(output_file)
        benchmarker.save_analysis_json()
        
        # Generar gráficas
        benchmarker.generate_plots()
        
        # Generar reporte HTML
        generate_html_report(DATA_DIR)
        
        # Mostrar resumen final
        successful = len([r for r in benchmarker.results if r.success])
        total = len(benchmarker.results)
        
        print(f"\n🎉 Benchmark completado exitosamente!")
        print(f"   Experimentos exitosos: {successful}/{total} ({successful/total:.1%})")
        print(f"   Resultados guardados en: {output_file}")
        print(f"   Análisis detallado en: {ANALYSIS_FILE}")
        print(f"   Reporte HTML en: {REPORT_FILE}")
        
        if HAS_MATPLOTLIB:
            print(f"   Gráficas en: {DATA_DIR}/plots/")
        
    except KeyboardInterrupt:
        benchmarker.log("WARN", "Benchmark interrumpido por el usuario")
        sys.exit(130)
    
    except Exception as e:
        benchmarker.log("ERROR", f"Error inesperado: {e}")
        if args.verbose:
            import traceback
            traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()