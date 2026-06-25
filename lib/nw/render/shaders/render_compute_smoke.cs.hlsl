// Minimal compute shader used to prove nw::gfx compute plumbing: it writes two
// distinct storage buffers so the multi-storage descriptor binding can be verified
// by reading the values back on the host.
RWStructuredBuffer<uint> g_out0 : register(u2); // storage slot 0 -> binding 2
RWStructuredBuffer<uint> g_out1 : register(u3); // storage slot 1 -> binding 3

[numthreads(1, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    (void)dispatch_thread_id;
    g_out0[0] = 0x1234u;
    g_out1[0] = 0x5678u;
}
