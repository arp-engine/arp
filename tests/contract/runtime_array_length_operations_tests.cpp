#include "../fixtures/runtime_array_length_operations_fixture.h"
#ifdef NDEBUG
#error Runtime length operations require assertions
#endif
int main(int argc, char** argv)
{
    assert(argc >= 3);
    arp::test::RuntimeArrayLengthOperationsFixture::real(
        argv[1], argv[2], argc > 3 && std::string_view(argv[3]) == "rw", argc > 4);
}
