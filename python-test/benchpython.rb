require_relative 'python_bencher'

stress = ARGV[0].to_i
stress= 1 if (stress==0)

#PythonBenchmark.run_all(true,stress)
#PythonBenchmark.perf_count_all(stress)
#PythonBenchmark.build_opt(".awabc",true)
#[".cgc",".abc",".fabc"].each {|opt| PythonBenchmark.build_opt(opt,true)}
#PythonBenchmark.run_loca_all(nil,true)
#puts PythonBenchmark.dump_runtimes(stress)
#PythonBenchmark.run_ref(stress)
