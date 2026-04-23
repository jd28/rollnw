// Minimal compute shader used to prove nw::gfx compute plumbing.
[numthreads(1, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    (void)dispatch_thread_id;
}
