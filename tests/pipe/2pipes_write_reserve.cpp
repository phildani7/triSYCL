/* RUN: %{execute}%s

   4 kernels producing, transforming and consuming data through 3 pipes
*/
#include <CL/sycl.hpp>
#include <iostream>
#include <iterator>

#include <boost/test/minimal.hpp>

// Size of the buffers
constexpr size_t N = 20;
// Number of work-item per work-group
constexpr size_t WI = 20;
static_assert(N == WI*(N/WI), "N needs to be a multiple of WI");

using Type = int;

int test_main(int argc, char *argv[]) {
  // Initialize the input buffers to some easy-to-compute values
  cl::sycl::buffer<Type> a { N };
  {
    auto aa = a.get_access<cl::sycl::access::write>();
    // Initialize buffer a with increasing integer numbers starting at 0
    std::iota(aa.begin(), aa.end(), 0);
  }

  // A buffer of N Type to get the result
  cl::sycl::buffer<Type> c { N };

  // The plumbin
  cl::sycl::pipe<Type> pa { WI };

  // Create a queue to launch the kernels
  cl::sycl::queue q;

  // Launch a producer for streaming va to the pipe pa
  q.submit([&] (cl::sycl::handler &cgh) {
      // Get write access to the pipe
      auto apa = pa.get_access<cl::sycl::access::write>(cgh);
      // Get read access to the data
      auto aa = a.get_access<cl::sycl::access::read>(cgh);
      /* Create a kernel with WI work-items executed by work-groups of
         size WI, that is only 1 work-group of WI work-items */
      cgh.parallel_for_work_group<class stream_a>(
        { WI, WI },
        [=] (auto group) {
          std::cerr << "kernel" << std::endl;
          // Use a sequential loop in the work-group to stream chunks in order
          for (int start = 0; start != N; start += WI) {
            /* To keep the reservation status outside the scope of the
               reservation itself */
            bool ok;
            do {
              // Try to reserve a chunk of WI elements of the pipe for writing
              auto r = apa.reserve(WI);
              // Evaluating the reservation as a bool returns the status
              ok = r;
              std::cerr << "ok = " << ok << std::endl;
              if (ok) {
//sleep(1000);
                /* There was enough room for the reservation, then
                   launch the work-items in this work-group to do the
                   writing in parallel */
                group.parallel_for_work_item([=] (cl::sycl::item<> i) {
                    std::cerr << "i[0] = " << i[0] << std::endl;
                    r[i[0]] = aa[start + i[0]];
                  });
              }
            }
            while (!ok);
          }
        });
    });

  // Launch the consumer to read stream from pipe pa to buffer c
  q.submit([&] (cl::sycl::handler &cgh) {
      // Get read access to the pipe
      auto apa = pa.get_access<cl::sycl::access::read>(cgh);
      // Get write access to the data
      auto ac = c.get_access<cl::sycl::access::write>(cgh);

      cgh.single_task<class consumer>([=] {
          for (int i = 0; i != N; i++)
            // Try to read 1 element from the pipe up to success
            while (!(apa >> ac[i])) ;
        });
    });

  // Verify on the host the buffer content
  for(auto const &e : c.get_access<cl::sycl::access::read>()) {
    std::cerr << e << ' ' << &e - &*c.get_access<cl::sycl::access::read>().begin() << std::endl;
    BOOST_CHECK(e == &e - &*c.get_access<cl::sycl::access::read>().begin());
  }
  return 0;
}
