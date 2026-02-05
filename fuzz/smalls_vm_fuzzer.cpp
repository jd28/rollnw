#include "smalls_fuzz_common.hpp"

#include "nw/kernel/Kernel.hpp"
#include "nw/smalls/AstCompiler.hpp"
#include "nw/smalls/Bytecode.hpp"
#include "nw/smalls/VirtualMachine.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    nw::smalls::fuzz::ensure_kernel_started();

    if (size == 0) {
        return 0;
    }

    try {
        std::string src = nw::smalls::fuzz::to_source(data, size);
        nw::smalls::fuzz::FuzzContext ctx;
        auto script = nw::smalls::fuzz::make_script(src, &ctx);
        script.parse();
        if (script.errors() != 0) {
            return 0;
        }

        script.resolve();
        if (script.errors() != 0) {
            return 0;
        }

        nw::smalls::BytecodeModule module("fuzz");
        auto& rt = nw::kernel::runtime();
        nw::smalls::AstCompiler compiler(&script, &module, &rt, &ctx);
        if (!compiler.compile()) {
            return 0;
        }

        const nw::smalls::CompiledFunction* main_fn = module.get_function("main");
        if (!main_fn || main_fn->param_count != 0) {
            return 0;
        }

        nw::smalls::VirtualMachine vm;
        vm.set_step_limit(20000);
        (void)vm.execute(&module, "main", {});
    } catch (...) {
        // Execution/compile failures are expected.
    }
    return 0;
}
