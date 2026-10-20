#include "../fixtures/original_runtime_length_pipeline_input_fixture.h"
#ifdef NDEBUG
#error Original available-target input tests require assertions
#endif
int main(int argc, char** argv)
{
    assert(argc >= 3);
    const std::filesystem::path path = argv[1];
    const bool graphics = path.filename().string().find(".graphics.module.") != std::string::npos;
    const arp::core::StringView mode = argv[2];
    arp::test::OriginalRuntimeLengthPipelineInputFixture::real(
        path, graphics, mode == "deleted", mode == "ordinary");
}
