require 'fileutils'
require 'open3'
require 'yaml'
PYTHON="/u/rlavaee/usr/local/bin/python2.7"
OPTS=[".cgc",".abc",".fabc"]
SUFFIXES=[".ni",".in"]
PIN="/p/compiler/Pin/pin-2.12-58423-gcc.4.4.7-linux/pin -t /p/compiler/Pin/pin-2.12-58423-gcc.4.4.7-linux/source/tools/Footprint/obj-intel64/dual_fp_all.so -m 2"
LOCA_INPUTS = {"django"=>"/u/rlavaee/benchmarks/performance/bm_django.py -n 1",
               "fastpickle"=>"/u/rlavaee/benchmarks/performance/bm_pickle.py -n 1 --use_cpickle pickle",
               "mako"=> "/u/rlavaee/benchmarks/performance/bm_mako.py -n 5",
               "nqueens"=>"/u/rlavaee/benchmarks/performance/bm_nqueens.py -n 1",
               "regex_compile"=>"/u/rlavaee/benchmarks/performance/bm_regex_compile.py -n 1",
               "slowpickle"=>"/u/rlavaee/benchmarks/performance/bm_pickle.py -n 1 pickle"}
LOCA_ITERATIONS = {"django"=>1, "fastpickle"=>1, "mako"=>5, "nqueens"=>1, "regex_compile"=>1, "slowpickle"=>1}

AllTrainBench="django fastpickle mako nqueens regex_compile slowpickle".split(' ')
AllBench=AllTrainBench
SAMPLE_RATES = {".abc"=>8, ".fabc"=>8, ".awabc"=>6, ".babc"=>6}

class PythonBenchmark
  @@PythonRoot="/u/rlavaee/Python-2.7.5"
  @@BenchRoot="/u/rlavaee/benchmarks"
  @@ResultDir="#{Dir.pwd}/results"

  attr_accessor :sr
  attr_accessor :mws
  attr_accessor :suffix
  attr_accessor :opt

  def initialize(bench)
    @bench=(bench.class==Array)?(bench):([bench])
    Dir.mkdir(@@ResultDir) unless Dir[@@ResultDir]!=[]
  end

  def bench_info
    if(@opt.nil? or @opt==".cgc")
      return @bench.join(',')
    else
      return "#{@sr}_#{@mws}_#{@bench.join(',')}"
    end
  end

  def get_suffix(ext,version,opt=@opt)
    if(version==".ref")
      return "#{ext}#{version}"
    else
      return "_trainedby_#{self.bench_info}#{ext}#{opt}#{version}"
    end
  end

  def PythonBenchmark.rebuild_tracer(suffix)
    Dir.chdir(@@PythonRoot)
    system "make -j 4 build-tracer#{suffix}"
  end

  def PythonBenchmark.rebuild_regular(suffix)
    Dir.chdir(@@PythonRoot)
    system "make -j 4 build-reg#{suffix}"
  end


  def PythonBenchmark.run_icc_ref		
    Dir.chdir(@@BenchRoot)
    AllBench.each do |input|
      output=`#{PYTHON} perf.py -v -b #{input} #{@@PythonRoot}/python2.7.orig.ref#{@suffix} #{@@PythonRoot}/python2.7.icc.ref#{@suffix}`
      FileUtils.cp("hw_cntrs.out", "#{@@ResultDir}/#{input}.icc.ref#{@suffix}")
    end
  end

  def train
    @bench.each do |benchpart|
      Dir.chdir(@@BenchRoot)
      output=`#{PYTHON} perf.py -v -f -b #{benchpart} --sr #{@sr} --mws #{@mws} #{@@PythonRoot}/python2.7.orig.ref#{@suffix} #{@@PythonRoot}/python2.7#{@opt}.tr#{@suffix}`
      File.open("#{@@ResultDir}/#{self.bench_info}#{@opt}.train#{@suffix}","w") {|f| f.write(output)}
    end
    FileUtils.mv("graph#{@opt}","#{@@ResultDir}/graph#{self.bench_info}#{@opt}#{@suffix}")
  end

  def run_version(input,ext,version)
    Dir.chdir(@@BenchRoot)
    if(ext==".orig")
      output=`#{PYTHON} perf.py -v -b #{input} #{@@PythonRoot}/python2.7.orig.ref#{@suffix} #{@@PythonRoot}/python2.7#{get_suffix(ext,version)}#{@suffix}` 
      File.open("#{@@ResultDir}/#{input}#{get_suffix(ext,version)}#{@suffix}","w") {|f| f.write(output)}
    elsif(ext==".icc")
      output=`#{PYTHON} perf.py -v -b #{input} #{@@PythonRoot}/python2.7.orig.ref#{@suffix} #{@@PythonRoot}/python2.7#{get_suffix(ext,version)}#{@suffix}`
      FileUtils.mv("hw_cntrs.out", "#{@@ResultDir}/#{input}#{get_suffix(ext,version)}#{@suffix}")
    end
  end

  def rebuild_optimized
    Dir.chdir(@@PythonRoot)
    system "sh -c 'export TARGET=\"_trainedby_#{self.bench_info}\"; make build-optimized#{@opt}#{@suffix}'" 
    FileUtils.mv("#{@@BenchRoot}/layout#{@opt}","#{@@ResultDir}/layout_#{self.bench_info}#{@opt}#{@suffix}")
  end

  def run_loca(input,version,run=false)
    loca_output_f = "#{@@ResultDir}/python2.7#{get_suffix(".orig",version)}#{@suffix}.#{input}.data"
    if(run)
      output = `#{PIN} -o #{loca_output_f} -- #{@@PythonRoot}/python2.7#{get_suffix(".orig",version)}#{@suffix} #{LOCA_INPUTS[input]} -n #{LOCA_ITERATIONS[input]}`
      puts "ran loca for #{version}: #{output}" 
    end
    return "#{loca_output_f}.i"
  end

  def run(input,ext,version,stress=1)
    puts input
    puts ext
    puts version
    Dir.chdir(@@BenchRoot)
    commands = Array.new
    proc_aff = 1
    binding = (stress > 1)?("taskset #{"%x" % proc_aff} "):("")
    stress.times.each do |i|
      commands << "#{binding}#{@@PythonRoot}/python2.7#{get_suffix(ext,version)}#{@suffix} #{LOCA_INPUTS[input]} -n #{LOCA_ITERATIONS[input]*20}"
    end

    pids = Array.new
    threads = Array.new
    stdouts = Array.new
    puts stress

    t1= Time.new.to_f
    commands.each do |command|
      stdin, stdout, stderr, thread = Open3.popen3(command)
      pids << thread.pid
      threads << thread
      stdouts << stdout
    end

    threads.each {|th| exit_status = th.value; puts "#{th.pid} finished"}
    puts stdouts[0].read
    t2= Time.new.to_f

    if(ext==".icc")
      counts = Hash.new
      pids.each do |pid|
        File.open("hw_cntrs_#{pid}.out","r") do |f|
          f.each_line do |line|
            arr = line.split("\t")
            event = arr[0]
            count = arr[1].to_i
            if(counts[event].nil?)
              counts[event]=count
            else
              counts[event]+=count
            end
          end
        end
      end

      File.open("#{@@ResultDir}/#{input}.stress#{stress}#{get_suffix(ext,version)}#{@suffix}","w") { |f| f.write(YAML.dump(counts))}
    else
      File.open("#{@@ResultDir}/#{input}.stress#{stress}#{get_suffix(ext,version)}#{@suffix}","w") {|f| f.write(YAML.dump(t2-t1))}
    end
  end

  def get_runtime(input,version,stress=1)
    File.open("#{@@ResultDir}/#{input}.stress#{stress}#{get_suffix(".orig",version)}#{@suffix}","r") {|f| f.readline}
  end

  def get_stress_counts(input,version,stress=1)
    YAML.load_file("#{@@ResultDir}/#{input}.stress#{stress}#{get_suffix(".icc",version)}#{@suffix}")
  end

  def dump_stress_counts(input,stress=1)
    str = "benchmark: #{input}\n"
    all_counts = Hash.new
    all_counts[".ref"] = get_stress_counts(input,".ref",stress)
    OPTS.each do |opt|
      @opt = opt
      @sr = SAMPLE_RATES[opt]
      all_counts["#{opt}.test"] = get_stress_counts(input,".test",stress)
    end

    ["L1_ICM","L2_ICM","TLB_IM","L2_TCM"].each do |event|
      str+= event+"\n"
      [".ref",".cgc.test",".abc.test",".fabc.test"].each do |version|
        str += "#{version}\t#{all_counts[version][event]}\n"
      end
      str += "\n"
    end
    str += "\n"
    return str
  end

  def PythonBenchmark.dump_runtimes(stress=1)
    str = ""
    AllTrainBench.each do |input|
      str += "benchmark: #{input}\n"
      pythonbench=PythonBenchmark.new(input)
      pythonbench.mws='12'
      pythonbench.suffix=".ni"
      str += ".ref\t"+pythonbench.get_runtime(input,".ref",stress)+"\n"
      [".cgc",".abc",".fabc"].each do |opt|
        pythonbench.sr=SAMPLE_RATES[opt]
        pythonbench.opt = opt
        str += opt+"\t"+pythonbench.get_runtime(input,".test",stress)+"\n"
      end
      str += "\n"
    end
    return str
  end

  def PythonBenchmark.perf_count_all(stress=1,run=false)
    FileUtils.rm("#{@@ResultDir}/stress#{stress}.icc") if File.exists?("#{@@ResultDir}/stress#{stress}.icc")
    AllTrainBench.each do |trainbench|
      r_args = Array.new
      pythonbench=PythonBenchmark.new(trainbench)
      pythonbench.suffix=".ni"
      [trainbench].each {|input| pythonbench.run(input,".icc",".ref",stress)} if(run)
      pythonbench.mws='12'
      OPTS.each do |opt|
        pythonbench.sr=SAMPLE_RATES[opt]
        puts pythonbench.sr
        pythonbench.opt = opt
        Dir.chdir("/u/rlavaee/benchmarks")
      end

      File.open("#{@@ResultDir}/stress#{stress}.icc","a+") do |f| 
        f.write(pythonbench.dump_stress_counts(trainbench,stress))
      end
    end

  end

  def PythonBenchmark.run_loca_all(opt=nil,run=false)
    AllTrainBench.each do |trainbench|
      r_args = Array.new
      pythonbench=PythonBenchmark.new(trainbench)
      pythonbench.suffix=".ni"
      [trainbench].each {|input| r_args << pythonbench.run_loca(input,".ref",run)}
      pythonbench.mws='12'
      opts = (opt.nil?)?(OPTS):([opt])
      opts.each do |opt|
        pythonbench.sr=SAMPLE_RATES[opt]
        pythonbench.opt = opt
        Dir.chdir("/u/rlavaee/benchmarks")
        [trainbench].each do |input|
          r_args << pythonbench.run_loca(input,".test",run)
        end
      end

      puts `Rscript ~/loca/server/draw_mr_fp.r #{r_args.join(' ')}`

    end

  end

  def PythonBenchmark.build_opt(opt,run=false)
    AllTrainBench.each do |trainbench|
      pythonbench=PythonBenchmark.new(trainbench)
      pythonbench.suffix=".ni"
      pythonbench.mws='12'
      pythonbench.opt = opt
      pythonbench.sr=SAMPLE_RATES[opt]
      pythonbench.train if(run)
      Dir.chdir("/u/rlavaee/benchmarks")
      [trainbench].each do |input|
        pythonbench.rebuild_optimized if(run)
        pythonbench.run(input,".orig",".test") if(run)
        pythonbench.run(input,".icc",".test") if(run)
      end
    end
  end

  def PythonBenchmark.run_ref(stress=1)
    AllTrainBench.each do |input|
      pythonbench=PythonBenchmark.new(input)
      pythonbench.suffix=".ni"
      pythonbench.run(input,".orig",".ref",stress)
    end
  end


  def PythonBenchmark.run_all(train=false,stress=1)
    AllTrainBench.each do |trainbench|
      pythonbench=PythonBenchmark.new(trainbench)
      pythonbench.suffix=".ni"
      pythonbench.mws='12'
      OPTS.each do |opt|
        pythonbench.opt = opt
        pythonbench.sr=SAMPLE_RATES[opt]
        pythonbench.train if(train)
        Dir.chdir("/u/rlavaee/benchmarks")
        [trainbench].each do |input|
          pythonbench.rebuild_optimized if(train)
          pythonbench.run(input,".orig",".test",stress)
          pythonbench.run(input,".icc",".test",stress)
        end
      end
    end
  end


end


