//developed by Mahmoud Khairy, Purdue Univ
//abdallm@purdue.edu

#include <time.h>
#include <stdio.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <stdio.h>
#include <math.h>

//#include "../abstract_hardware_model.h"
//#include "../option_parser.h"
//#include "../cuda-sim/cuda-sim.h"
//#include "../cuda-sim/ptx_ir.h"
//#include "../cuda-sim/ptx_parser.h"
#include "../gpgpu-sim/gpu-sim.h"
//#include "../gpgpu-sim/icnt_wrapper.h"
//#include "../gpgpu-sim/icnt_wrapper.h"
#include "../../libcuda/gpgpu_context.h"
#include "trace_driven.h"
#include "trace_opcode.h"

//#include "../stream_manager.h"


void arguments_check();

int main ( int argc, const char **argv )
{

	gpgpu_context* m_gpgpu_context = GPGPU_Context();
	gpgpu_sim * m_gpgpu_sim = m_gpgpu_context->gpgpu_trace_sim_init_perf(argc,argv);
	m_gpgpu_sim->init();

	//for each kernel
	//load file
	//parse and create kernel info
	//launch
	//while loop till the end of the end kernel execution
	//prints stats

	trace_parser tracer(m_gpgpu_sim->get_config().get_traces_filename(), m_gpgpu_sim, m_gpgpu_context);

	std::vector<std::string> kernellist = tracer.parse_kernellist_file();

	for(unsigned i=0; i<kernellist.size(); ++i) {

		trace_kernel_info_t* kernel_info  = tracer.parse_kernel_info(kernellist[i]);
		m_gpgpu_sim->launch(kernel_info);

		bool active = false;
		bool sim_cycles = false;
		bool break_limit = false;

		do {
			if(!m_gpgpu_sim->active())
				break;

			//performance simulation
			if( m_gpgpu_sim->active() ) {
				m_gpgpu_sim->cycle();
				sim_cycles = true;
				m_gpgpu_sim->deadlock_check();
			}else {
				if(m_gpgpu_sim->cycle_insn_cta_max_hit()){
					m_gpgpu_context->the_gpgpusim->g_stream_manager->stop_all_running_kernels();
					break_limit = true;
				}
			}

			active=m_gpgpu_sim->active() ;

		} while( active );

		tracer.kernel_finalizer(kernel_info);

		m_gpgpu_sim->print_stats();

		if(sim_cycles) {
			m_gpgpu_sim->update_stats();
			m_gpgpu_context->print_simulation_time();
		}

		if(break_limit) {
			printf("GPGPU-Sim: ** break due to reaching the maximum cycles (or instructions) **\n");
			fflush(stdout);
			exit(1);
		}
	}

	return 1;
}

trace_parser::trace_parser(const char* kernellist_filepath, gpgpu_sim * m_gpgpu_sim, gpgpu_context* m_gpgpu_context)
{

	this->m_gpgpu_sim = m_gpgpu_sim;
	this->m_gpgpu_context = m_gpgpu_context;
	kernellist_filename = kernellist_filepath;
}

std::vector<std::string> trace_parser::parse_kernellist_file() {

	ifs.open(kernellist_filename);

	if (!ifs.is_open()) {
		std::cout << "Unable to open file: " <<kernellist_filename<<std::endl;
		exit(1);
	}

	std::string directory(kernellist_filename);
	const size_t last_slash_idx = directory.rfind('/');
	if (std::string::npos != last_slash_idx)
	{
		directory = directory.substr(0, last_slash_idx);
	}

	std::string line, filepath;
	std::vector<std::string> kernellist;
	while(!ifs.eof()) {
		getline(ifs, line);
		if(line.empty())
			continue;
		filepath = directory+"/"+line;
		kernellist.push_back(filepath);
	}

	ifs.close();
	return kernellist;
}


trace_kernel_info_t* trace_parser::parse_kernel_info(const std::string& kerneltraces_filepath) {

	ifs.open(kerneltraces_filepath.c_str());

	if (!ifs.is_open()) {
		std::cout << "Unable to open file: " <<kerneltraces_filepath<<std::endl;
		exit(1);
	}

	std::cout << "Processing kernel " <<kerneltraces_filepath<<std::endl;

	unsigned grid_dim_x=0, grid_dim_y=0, grid_dim_z=0, tb_dim_x=0, tb_dim_y=0, tb_dim_z=0;
	unsigned shmem=0, nregs=0, cuda_stream_id=0, kernel_id=0;
	std::string line;
	std::stringstream ss;
	std::string string1, string2;
	std::string  kernel_name;

	while(!ifs.eof()) {
		getline(ifs, line);

		if (line.length() == 0) {
			continue;
		}
		else if(line[0] == '#'){
			//the trace format, ignore this and assume fixed format for now
			break;  //the begin of the instruction stream
		}
		else if(line[0] == '-') {
			ss.str(line);
			ss.ignore();
			ss>>string1>>string2;
			if(string1 == "kernel" && string2 == "name") {
				const size_t equal_idx = line.find('=');
				kernel_name = line.substr(equal_idx+1);
			}
			else if(string1 == "kernel" && string2 == "id") {
				sscanf(line.c_str(), "-kernel id = %d", &kernel_id);
			}
			else if(string1 == "grid" && string2 == "dim") {
				sscanf(line.c_str(), "-grid dim = (%d,%d,%d)", &grid_dim_x, &grid_dim_y, &grid_dim_z);
			}
			else if (string1 == "block" && string2 == "dim") {
				sscanf(line.c_str(), "-block dim = (%d,%d,%d)", &tb_dim_x, &tb_dim_y, &tb_dim_z);
			}
			else if (string1 == "shmem") {
				sscanf(line.c_str(), "-shmem = %d", &shmem);
			}
			else if (string1 == "nregs") {
				sscanf(line.c_str(), "-nregs = %d", &nregs);
			}
			else if (string1 == "cuda" && string2 == "stream") {
				sscanf(line.c_str(), "-cuda stream id = %d", &cuda_stream_id);
			}
			continue;
		}
	}

	gpgpu_ptx_sim_info info;
	info.smem = shmem;
	info.regs = nregs;
	dim3 gridDim(grid_dim_x, grid_dim_y, grid_dim_z);
	dim3 blockDim(tb_dim_x, tb_dim_y, tb_dim_z);
	trace_function_info* function_info = new trace_function_info(info, m_gpgpu_context);
	function_info->set_name(kernel_name.c_str());
	trace_kernel_info_t* kernel_info =  new trace_kernel_info_t(gridDim, blockDim, function_info, &ifs, m_gpgpu_sim, m_gpgpu_context);

	return kernel_info;
}


void trace_parser::kernel_finalizer(trace_kernel_info_t* kernel_info){
	if (ifs.is_open())
		ifs.close();

	delete kernel_info->entry();
	delete kernel_info;
}

bool trace_kernel_info_t::get_next_threadblock_traces(std::vector<std::vector<trace_warp_inst_t>>& threadblock_traces) {

	threadblock_traces.clear();
	unsigned warps_per_tb = ceil(float(threads_per_cta()/32));
	threadblock_traces.resize(warps_per_tb);

	unsigned block_id_x=0, block_id_y=0, block_id_z=0;
	unsigned warp_id=0;
	unsigned insts_num=0;
	std::string line;
	std::stringstream ss;
	std::string string1, string2;

	bool start_of_tb_stream_found = false;

	while(!ifs->eof()) {
		getline(*ifs, line);

		if (line.length() == 0) {
			continue;
		}
		else {
			ss.str(line);
			ss>>string1>>string2;
			if (string1 == "#BEGIN_TB") {
				if(!start_of_tb_stream_found)
					start_of_tb_stream_found=true;
				else assert(0 && "Parsing error: thread block start before the previous one finish");
			}
			else if (string1 == "#END_TB") {
				assert(start_of_tb_stream_found);
				break; //end of TB stream
			}
			else if(string1 == "thread" && string2 == "block") {
				assert(start_of_tb_stream_found);
				sscanf(line.c_str(), "thread block = %d,%d,%d", &block_id_x, &block_id_y, &block_id_z);
			}
			else if (string1 == "warp") {
				//the start of new warp stream
				assert(start_of_tb_stream_found);
				sscanf(line.c_str(), "warp = %d", &warp_id);
			}
			else if (string1 == "insts") {
				assert(start_of_tb_stream_found);
				sscanf(line.c_str(), "insts = %d", &insts_num);
				threadblock_traces[warp_id].resize(insts_num);
			}
			else {
				assert(start_of_tb_stream_found);
				trace_warp_inst_t inst(m_gpgpu_sim->getShaderCoreConfig(), m_gpgpu_context);
				inst.parse_from_string(line);
				threadblock_traces[warp_id].push_back(inst);
			}
		}
	}

	return true;
}


bool trace_warp_inst_t::parse_from_string(std::string trace){

	std::stringstream ss;
	ss.str(trace);


	std::string temp;
	unsigned threadblock_x=0, threadblock_y=0, threadblock_z=0, warpid_tb=0, sm_id=0, warpid_sm=0;
	unsigned long long m_pc=0;
	unsigned mask=0;
	unsigned reg_dest=0;
	std::string opcode;
	unsigned reg_srcs_num=0;
	unsigned reg_srcs[4];
	unsigned mem_width=0;
	unsigned long long mem_addresses[warp_size()];

	ss>>std::dec>>threadblock_x>>threadblock_y>>threadblock_z>>warpid_tb>>sm_id>>warpid_sm;

	ss>>std::hex>>m_pc>>mask;
	std::bitset<MAX_WARP_SIZE> mask_bits(mask);

	ss>>std::dec>>temp;
	sscanf(temp.c_str(), "R%d", &reg_dest);

	ss>>opcode;
	ss>>reg_srcs_num;

	for(unsigned i=0; i<reg_srcs_num; ++i) {
		ss>>temp;
		sscanf(temp.c_str(), "R%d", &reg_srcs[i]);
	}

	ss>>mem_width;
	if(mem_width > 0)  //then it is a memory inst
	{
		for (int s = 0; s < warp_size(); s++) {
			if(mask_bits.test(s))
				ss>>std::hex>>mem_addresses[s];
			else
				mem_addresses[s]=0;
		}
	}

	//After parsing, fill the inst_t and warp_inst_t params

	//fill active mask
	active_mask_t active_mask = mask_bits;
	set_active( active_mask );

	//get the opcode
	std::string opcode1 = opcode.substr(0, opcode.find("."));

	//fill and initialize common params
	m_decoded = true;
	pc = m_pc;
	isize = 16;   //TO DO, change this
	for(unsigned i=0; i<MAX_OUTPUT_VALUES; i++) {
		out[i] = 0;
	}
	for(unsigned i=0; i<MAX_INPUT_VALUES; i++) {
		in[i] = 0;
	}

	is_vectorin = 0;
	is_vectorout = 0;
	ar1 = 0;
	ar2 = 0;
	memory_op = no_memory_op;
	data_size = 0;

	num_regs = reg_srcs_num+1;
	num_operands = num_regs;
	op = ALU_OP;
	mem_op= NOT_TEX;


	//fill regs information
	outcount=1;
	out[0]=reg_dest;
	arch_reg.dst[0]=reg_dest;

	incount=reg_srcs_num;
	for(unsigned m=0; m<reg_srcs_num; ++m){
		in[m]=reg_srcs[m];
		arch_reg.src[m]=reg_srcs[m];
	}
	//handle: vector, store insts have no output, double inst and hmma, and 64 bit address


	//get opcode and category
	std::unordered_map<const char*,OpcodeChar>::const_iterator it= OpcodeMap.find(opcode1.c_str());
	if (it != OpcodeMap.end()) {
		m_opcode = it->second.opcode;
		op = (op_type)(it->second.opcode_category);
	}
	else
		assert(0 && "undefined instruction");


	//fill latency and initl
	set_latency(op);

	//fill addresses
	if(mem_width > 0) {
		for(unsigned i=0; i<warp_size(); ++i)
			set_addr(i, mem_addresses[i]);
	}

	//fill memory space
	switch(m_opcode){
	case OP_LD:
	case OP_LDG:
	case OP_LDL:
		assert(mem_width>0);
		data_size = mem_width;
		memory_op = memory_load;
		if(m_opcode == OP_LDL)
			space.set_type(local_space);
		else
			space.set_type(global_space);
		break;
	case OP_ST:
	case OP_STG:
	case OP_STL:
	case OP_ATOM:
	case OP_ATOMG:
	case OP_RED:
		assert(mem_width>0);
		data_size = mem_width;
		memory_op = memory_store;
		if(m_opcode == OP_STL)
			space.set_type(local_space);
		else
			space.set_type(global_space);

		if(m_opcode == OP_ATOM || m_opcode == OP_ATOMG || m_opcode == OP_RED)
			m_isatomic = true;
		break;
	case OP_LDS:
	case OP_STS:
	case OP_ATOMS:
		assert(mem_width>0);
		data_size = mem_width;
		space.set_type(shared_space);
		break;

	default:
			break;
	}

	return true;
}

void trace_warp_inst_t::set_latency(unsigned category)
{
	unsigned int_latency[5];
	unsigned fp_latency[5];
	unsigned dp_latency[5];
	unsigned sfu_latency;
	unsigned tensor_latency;
	unsigned int_init[5];
	unsigned fp_init[5];
	unsigned dp_init[5];
	unsigned sfu_init;
	unsigned tensor_init;

	/*
	 * [0] ADD,SUB
	 * [1] MAX,Min
	 * [2] MUL
	 * [3] MAD
	 * [4] DIV
	 */
	sscanf(m_gpgpu_context->func_sim->opcode_latency_int, "%u,%u,%u,%u,%u",
			&int_latency[0],&int_latency[1],&int_latency[2],
			&int_latency[3],&int_latency[4]);
	sscanf(m_gpgpu_context->func_sim->opcode_latency_fp, "%u,%u,%u,%u,%u",
			&fp_latency[0],&fp_latency[1],&fp_latency[2],
			&fp_latency[3],&fp_latency[4]);
	sscanf(m_gpgpu_context->func_sim->opcode_latency_dp, "%u,%u,%u,%u,%u",
			&dp_latency[0],&dp_latency[1],&dp_latency[2],
			&dp_latency[3],&dp_latency[4]);
	sscanf(m_gpgpu_context->func_sim->opcode_latency_sfu, "%u",
			&sfu_latency);
	sscanf(m_gpgpu_context->func_sim->opcode_latency_tensor, "%u",
			&tensor_latency);
	sscanf(m_gpgpu_context->func_sim->opcode_initiation_int, "%u,%u,%u,%u,%u",
			&int_init[0],&int_init[1],&int_init[2],
			&int_init[3],&int_init[4]);
	sscanf(m_gpgpu_context->func_sim->opcode_initiation_fp, "%u,%u,%u,%u,%u",
			&fp_init[0],&fp_init[1],&fp_init[2],
			&fp_init[3],&fp_init[4]);
	sscanf(m_gpgpu_context->func_sim->opcode_initiation_dp, "%u,%u,%u,%u,%u",
			&dp_init[0],&dp_init[1],&dp_init[2],
			&dp_init[3],&dp_init[4]);
	sscanf(m_gpgpu_context->func_sim->opcode_initiation_sfu, "%u",
			&sfu_init);
	sscanf(m_gpgpu_context->func_sim->opcode_initiation_tensor, "%u",
			&tensor_init);
	sscanf(m_gpgpu_context->func_sim->cdp_latency_str, "%u,%u,%u,%u,%u",
			&m_gpgpu_context->func_sim->cdp_latency[0],
			&m_gpgpu_context->func_sim->cdp_latency[1],
			&m_gpgpu_context->func_sim->cdp_latency[2],
			&m_gpgpu_context->func_sim->cdp_latency[3],
			&m_gpgpu_context->func_sim->cdp_latency[4]);

	initiation_interval = latency = 1;

	switch(category){
		case ALU_OP:
		case INTP_OP:
		case BRANCH_OP:
		case CALL_OPS:
		case RET_OPS:
			latency = int_latency[0];
			initiation_interval = int_init[0];
			break;
		case SP_OP:
			latency = fp_latency[0];
			initiation_interval = fp_latency[0];
			break;
		case DP_OP:
			latency = dp_latency[0];
			initiation_interval = dp_latency[0];
			break;
		case SFU_OP:
			latency = sfu_latency;
			initiation_interval = sfu_init;
			break;
		case TENSOR_CORE_OP:
			latency = tensor_latency;
			initiation_interval = tensor_init;
			break;
		default:
				break;
		}

}


