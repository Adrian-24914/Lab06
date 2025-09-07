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