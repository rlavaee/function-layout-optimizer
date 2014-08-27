require_relative '../python_bencher'

stress = ARGV[0].to_i
stress= 1 if (stress==0)
#PythonBenchmark.rebuild_tracer(".ni")
#[1,2,4,6,8,10].each do |stress|
#	PythonBenchmark.run_ref(stress)
#end
#PythonBenchmark.run_all(true,stress)
#PythonBenchmark.perf_count_all(stress)
#[".cgc",".abc"].each {|opt| PythonBenchmark.build_opt(opt,true)}
#PythonBenchmark.run_loca_all(nil,true)
#puts PythonBenchmark.dump_runtimes(stress)
[2,4,6,8,10].each do |stress|
	PythonBenchmark.run_sens_wsize(false,stress)
end
