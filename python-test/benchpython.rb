require_relative 'python_bencher'

#stress = ARGV[0].to_i
#stress= 1 if (stress==0)
program = ARGV[0]
#PythonBenchmark.rebuild_tracer(".in")
#PythonBenchmark.rebuild_pg(".in")
#PythonBenchmark.rebuild_regular(".in")
#PythonBenchmark.run_pg(".in",false)
#PythonBenchmark.run_ref(stress)
#PythonBenchmark.run_all(false,stress)
PythonBenchmark.run_loca("slowpickle",program,true)
#PythonBenchmark.perf_count_all(stress)
#[".abc"].each {|opt| PythonBenchmark.build_opt(opt,true)}
#PythonBenchmark.run_loca_all(nil,false)
#puts PythonBenchmark.dump_runtimes(stress)
