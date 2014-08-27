require_relative '../python_bencher'

stress = ARGV[0].to_i
stress= 1 if (stress==0)
#PythonBenchmark.rebuild_tracer(".ni")
#PythonBenchmark.run_ref(stress)
#PythonBenchmark.run_all(true,stress)
#PythonBenchmark.perf_count_all(stress)
#[".cgc",".abc"].each {|opt| PythonBenchmark.build_opt(opt,true)}
#PythonBenchmark.run_loca_all(nil,true)
#puts PythonBenchmark.dump_runtimes(stress)
#puts PythonBenchmark.sens_wsize_dump_runtimes(stress)
puts PythonBenchmark.sens_wsize_dump_cache_counts(stress)



