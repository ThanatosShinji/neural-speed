#include <stdio.h>
#include "bestla_wrapper.h"
#include "bestla_ut.h"
#include "sycl_ut.h"
#include "sycl/sycl_wrapper.h"

namespace bestla {
using namespace ut;
using namespace utils;
using namespace sycl_utils;
using namespace sycl_gemm;
namespace sycl_ut {
int constexpr TestMs = 1000;
class Benchmark_Fp32Fp32 {
 public:
  Benchmark_Fp32Fp32() {
    UT_START();
    benchmark_all(1024, 4096, 4096);
    benchmark_all(4096, 4096, 4096);
  }

  using AType = float;
  using BType = float;
  using CType = float;
  using SGemmT = xve::DefaultSGemmCore;
  template <class GCT>
  using ProAT = sycl_prologue_a::ActivationBase<GCT, float>;
  template <class GCT>
  using ProBT = sycl_prologue_b::WeightBase<GCT, float>;
  template <class GCT>
  using EpiT = sycl_epilogue::OutputBase<GCT, float>;
  using KernelLauncher = sycl_wrapper::Launcher<ProAT, ProBT, EpiT, SGemmT>;

  template <typename LOG_T>
  void benchmark(int m, int n, int k, int batch, AType* A, BType* B, CType* C, float timems) {
    LOG_T log;
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    utils::timer<std::chrono::milliseconds> tm;
    auto A_d = A;
    auto B_d = B;
    auto C_d = C;
    auto psize = (size_t)m * n * k * 2;
    sycl::range<2> group{SGemmT::WgM, SGemmT::WgN};
    sycl::range<2> problem{m / SGemmT::TileM, n / SGemmT::TileN};
    utils::GemmProblem gp(1, m, n, k);
    tm.start();
    while (tm.stop() < timems) {
      for (size_t i = 0; i < batch; i++) {
        auto e_esimd = KernelLauncher::compute({m, n, k, {A, k}, {B, n}, {C, n}}, q);
        e_esimd.wait();
        log.add(event_helper::execute_time(e_esimd) * 1000);
        if (tm.stop() >= timems) {
          break;
        }
      }
    }
    log.record();
    double flops = double(psize) / log.min_val / 1e6;
    printf(" %s Flops:%.3f\n", log.get_log_str(), flops);
  }

  void benchmark_all(int m, int n, int k) {
    auto memsize = gemm_memsize(m, n, k, BTLA_DTYPE::F32, BTLA_DTYPE::F32, BTLA_DTYPE::F32);
    auto batch = auto_batch(memsize);
    printf("%d %d %d %d %s %s %s\n", m, n, k, batch, bestla_dtype_str(BTLA_DTYPE::F32),
           bestla_dtype_str(BTLA_DTYPE::F32), bestla_dtype_str(BTLA_DTYPE::F32));
    avector<AType> A(size_t(m) * k * batch);
    avector<BType> B(size_t(k) * n * batch);
    avector<CType> C(size_t(m) * n * batch, 0);
    fill_buffer_randn(A.data(), m * k, -0.5f, 0.5f);
    fill_buffer_randn(B.data(), n * k, -0.5f, 0.5f);
    for (size_t i = 0; i < batch - 1; i++) {
      memcpy(A.data() + i * m * k, A.data(), m * k * sizeof(AType));
      memcpy(B.data() + i * n * k, B.data(), n * k * sizeof(BType));
    }
    using LOG = timer_statistics_logger<TestMs * 2>;
    float testtime = float(TestMs);
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    sycl_vector<float> dA(A.size(), q), dB(B.size(), q), dC(C.size(), q);
    q->memcpy(dA.data(), A.data(), A.size() * 4).wait();
    q->memcpy(dB.data(), B.data(), B.size() * 4).wait();

    benchmark<LOG>(m, n, k, batch, dA.data(), dB.data(), dC.data(), testtime);
  }
};
// static Benchmark_Fp32Fp32 sBenchmark_Fp32Fp32;

class Benchmark_S4Fp32Fp32 {
 public:
  Benchmark_S4Fp32Fp32() {
    UT_START();
    benchmark_all(1, 4480, 4096);
    benchmark_all(1, 4480, 4096 * 3);
    benchmark_all(1, 4096 * 3, 4096);
    benchmark_all(1024, 4096, 4096);
    benchmark_all(4096, 4096, 4096);
  }

  using AType = float;
  using BType = float;
  using CType = float;
  using SGemmT = xve::DefaultSGemmCore;
  template <class GCT>
  using ProAT = sycl_prologue_a::ActivationBase<GCT, float>;
  template <class GCT>
  using ProBT = sycl_prologue_b::WeightS4<GCT, float>;
  template <class GCT>
  using ProBTransT = sycl_prologue_b::WeightS4Trans<GCT, float>;
  template <class GCT>
  using EpiT = sycl_epilogue::OutputBase<GCT, float>;
  using KernelLauncher = sycl_wrapper::LauncherWOQ<ProAT, ProBT, EpiT, SGemmT>;
  using KernelLauncherT = sycl_wrapper::LauncherWOQ<ProAT, ProBTransT, EpiT, SGemmT>;

  template <typename LOG_T>
  void benchmark(int m, int n, int k, int batch, AType* A, uint8_t* B, float* B_scale, CType* C, float timems) {
    LOG_T log;
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    utils::timer<std::chrono::milliseconds> tm;
    auto A_d = A;
    auto B_d = B;
    auto C_d = C;
    auto psize = (size_t)m * n * k * 2;
    sycl::range<2> group{SGemmT::WgM, SGemmT::WgN};
    sycl::range<2> problem{m / SGemmT::TileM, n / SGemmT::TileN};
    utils::GemmProblem gp(1, m, n, k);
    tm.start();
    while (tm.stop() < timems) {
      for (size_t i = 0; i < batch; i++) {
        auto e_esimd = KernelLauncher::compute({m, n, k, 32, {A, k}, {B, B_scale, n}, {C, n}}, q);
        e_esimd.wait();
        log.add(event_helper::execute_time(e_esimd) * 1000);
        if (tm.stop() >= timems) {
          break;
        }
      }
    }
    log.record();
    double flops = double(psize) / log.min_val / 1e6;
    printf(" %s Flops:%.3f\n", log.get_log_str(), flops);
  }

  template <typename LOG_T>
  void benchmarkT(int m, int n, int k, int batch, AType* A, uint8_t* B, float* B_scale, CType* C, float timems) {
    LOG_T log;
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    utils::timer<std::chrono::milliseconds> tm;
    auto A_d = A;
    auto B_d = B;
    auto C_d = C;
    auto psize = (size_t)m * n * k * 2;
    int blks = k / 32;
    tm.start();
    while (tm.stop() < timems) {
      for (size_t i = 0; i < batch; i++) {
        auto e_esimd = KernelLauncherT::compute({m, n, k, 32, {A, k}, {B, B_scale, blks}, {C, n}}, q);
        e_esimd.wait();
        log.add(event_helper::execute_time(e_esimd) * 1000);
        if (tm.stop() >= timems) {
          break;
        }
      }
    }
    log.record();
    double flops = double(psize) / log.min_val / 1e6;
    printf(" %s Flops:%.3f\n", log.get_log_str(), flops);
  }

  template <typename LOG_T>
  void benchmark_gemv(int m, int n, int k, int batch, AType* A, uint8_t* B, float* B_scale, CType* C, float timems) {
    LOG_T log;
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    utils::timer<std::chrono::milliseconds> tm;
    auto A_d = A;
    auto B_d = B;
    auto C_d = C;
    auto S_d = B_scale;
    auto psize = (size_t)m * n * k * 2;
    int constexpr blocksize = 32;
    sycl::range<2> group{SGemmT::WgM, SGemmT::WgN};
    sycl::range<2> problem{m / SGemmT::TileM, n / SGemmT::TileN};
    utils::GemmProblem gp(1, m, n, k);
    tm.start();
    while (tm.stop() < timems) {
      for (size_t i = 0; i < batch; i++) {
        int constexpr SgSize = 16;
        int constexpr KSlicing = 16;
        int constexpr TileN = 2;
        int constexpr GroupN = SgSize * TileN;
        sycl::range<1> group{KSlicing * SgSize};
        sycl::range<1> problem{KSlicing * n / TileN};
        auto n_blks = updiv(n / TileN, SgSize);
        int sg_ksize = k / KSlicing;
        auto e_esimd = q->submit([&](sycl::handler& cgh) {
          sycl::local_accessor<float, 1> slm(sycl::range(GroupN * (KSlicing - 1)), cgh);
          cgh.parallel_for(sycl::nd_range<1>(problem, group),
                           [=](sycl::nd_item<1> it) [[intel::reqd_sub_group_size(SgSize)]] {
                             int g_idx = it.get_group(0);
                             auto sg = it.get_sub_group();
                             int sg_group_id = sg.get_group_id()[0];
                             int sg_id = sg.get_local_id()[0];
                             int g_n = g_idx * GroupN;
                             int sg_k = sg_group_id * sg_ksize;
                             auto sptr = S_d + sg_k / blocksize * n + g_n;
                             auto bptr = B_d + (sg_k * n + g_n) / 2;
                             auto aptr = A_d + sg_k;
                             auto cptr = C_d + g_n;
                             float tmpAcc[TileN];
#pragma unroll
                             for (int i = 0; i < TileN; i++) {
                               tmpAcc[i] = 0.f;
                             }
                             for (int i = 0; i < sg_ksize; i += blocksize) {
                               float localAcc[TileN];
                               for (int i = 0; i < TileN; i++) {
                                 localAcc[i] = 0.f;
                               }
#pragma unroll
                               for (int ik = 0; ik < blocksize; ik++) {
                                 uint8_t tmp = *(bptr + ik * n / 2 + sg_id);
                                 float tmpf[TileN];
                                 tmpf[0] = static_cast<int8_t>((tmp & 0x0f) << 4);
                                 tmpf[1] = static_cast<int8_t>((tmp & 0xf0));
                                 auto tmpA = aptr[ik];
#pragma unroll
                                 for (int in = 0; in < TileN; in++) {
                                   localAcc[in] += tmpf[in] * tmpA;
                                 }
                               }
                               for (int in = 0; in < TileN; in++) {
                                 tmpAcc[in] += localAcc[in] * *(sptr + sg_id * TileN + in);
                               }
                               sptr += n;
                               aptr += blocksize;
                               bptr += blocksize * n / 2;
                             }
                             int slm_idx = sg_group_id - 1;
                             if (slm_idx >= 0) {
#pragma unroll
                               for (int in = 0; in < TileN; in++) {
                                 slm[slm_idx * GroupN + sg_id * TileN + in] = tmpAcc[in];
                               }
                             }
                             it.barrier(sycl::access::fence_space::local_space);
                             if (sg_group_id == 0) {
#pragma unroll
                               for (int is = 0; is < KSlicing - 1; is++) {
#pragma unroll
                                 for (int in = 0; in < TileN; in++) {
                                   tmpAcc[in] += slm[is * GroupN + sg_id * TileN + in];
                                 }
                               }
#pragma unroll
                               for (int in = 0; in < TileN; in++) {
                                 cptr[sg_id * TileN + in] = tmpAcc[in];
                               }
                             }
                           });
        });
        e_esimd.wait();
        log.add(event_helper::execute_time(e_esimd) * 1000);
        if (tm.stop() >= timems) {
          break;
        }
      }
    }
    log.record();
    double flops = double(psize) / log.min_val / 1e6;
    printf(" %s Flops:%.3f\n", log.get_log_str(), flops);
  }

  template <typename LOG_T>
  void benchmark_gemv_T(int m, int n, int k, int batch, AType* A, uint8_t* B, float* B_scale, CType* C, float timems) {
    LOG_T log;
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    utils::timer<std::chrono::milliseconds> tm;
    auto A_d = A;
    auto B_d = B;
    auto C_d = C;
    auto S_d = B_scale;
    auto psize = (size_t)m * n * k * 2;
    int constexpr blocksize = 32;
    int blks = k / blocksize;
    sycl::range<2> group{SGemmT::WgM, SGemmT::WgN};
    sycl::range<2> problem{m / SGemmT::TileM, n / SGemmT::TileN};
    utils::GemmProblem gp(1, m, n, k);
    tm.start();
    while (tm.stop() < timems) {
      for (size_t i = 0; i < batch; i++) {
        int constexpr SgSize = 16;
        int constexpr TileK = 2;
        int constexpr GroupK = SgSize * TileK;
        sycl::range<1> group{SgSize};
        sycl::range<1> problem{n * SgSize};
        auto e_esimd = q->submit([&](sycl::handler& cgh) {
          cgh.parallel_for(sycl::nd_range<1>(problem, group),
                           [=](sycl::nd_item<1> it) [[intel::reqd_sub_group_size(SgSize)]] {
                             int g_idx = it.get_group(0);
                             auto sg = it.get_sub_group();
                             int sg_id = sg.get_local_id()[0];
                             int g_n = g_idx;
                             auto sptr = S_d + g_n * blks;
                             auto bptr = B_d + g_n * k / 2;
                             auto aptr = A_d;
                             auto cptr = C_d + g_n;
                             float tmpAcc[TileK];
#pragma unroll
                             for (int i = 0; i < TileK; i++) {
                               tmpAcc[i] = 0.f;
                             }
                             for (int i = 0; i < k; i += blocksize) {
                               float localAcc[TileK];
                               for (int i = 0; i < TileK; i++) {
                                 localAcc[i] = 0.f;
                               }
                               auto scale = *sptr;
                               for (int ik = 0; ik < blocksize; ik += GroupK) {
                                 uint8_t tmp = *(bptr + ik / 2 + sg_id);
                                 float tmpf[TileK];
                                 tmpf[0] = static_cast<int8_t>((tmp & 0x0f) << 4);
                                 tmpf[1] = static_cast<int8_t>((tmp & 0xf0));
#pragma unroll
                                 for (int in = 0; in < TileK; in++) {
                                   localAcc[in] += tmpf[in] * aptr[sg_id * TileK + in];
                                 }
                               }
                               for (int in = 0; in < TileK; in++) {
                                 tmpAcc[in] += localAcc[in] * scale;
                               }
                               sptr += 1;
                               aptr += blocksize;
                               bptr += blocksize / 2;
                             }
                             tmpAcc[0] += tmpAcc[1];
                             auto sum = 0.f;
                             for (int i = 0; i < SgSize; i++) {
                               sum += sg.shuffle(tmpAcc[0], i);
                             }
                             if (sg_id == 0) {
                               *cptr = sum;
                             }
                           });
        });
        e_esimd.wait();
        log.add(event_helper::execute_time(e_esimd) * 1000);
        if (tm.stop() >= timems) {
          break;
        }
      }
    }
    log.record();
    double flops = double(psize) / log.min_val / 1e6;
    printf(" %s Flops:%.3f\n", log.get_log_str(), flops);
  }

  template <typename LOG_T>
  void benchmark_gemv_T2(int m, int n, int k, int batch, AType* A, uint8_t* B, float* B_scale, CType* C, float timems) {
    LOG_T log;
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    utils::timer<std::chrono::milliseconds> tm;
    auto A_d = (const AType*)A;
    auto B_d = B;
    auto C_d = C;
    auto S_d = B_scale;
    auto psize = (size_t)m * n * k * 2;
    int constexpr blocksize = 32;
    int blks = k / blocksize;
    tm.start();
    while (tm.stop() < timems) {
      for (size_t i = 0; i < batch; i++) {
        int constexpr SgSize = 16;
        int constexpr TileK = 32;
        int constexpr GroupK = SgSize * TileK;
        sycl::range<1> group{SgSize};
        sycl::range<1> problem{n * SgSize};
        auto e_esimd = q->submit([&](sycl::handler& cgh) {
          cgh.parallel_for(sycl::nd_range<1>(problem, group),
                           [=](sycl::nd_item<1> it)
                               [[cl::reqd_work_group_size(1, 1, SgSize)]] [[intel::reqd_sub_group_size(SgSize)]] {
                                 int g_idx = it.get_group(0);
                                 auto sg = it.get_sub_group();
                                 int sg_id = sg.get_local_id()[0];
                                 int g_n = g_idx;
                                 auto sptr = S_d + g_n * blks;
                                 auto bptr = B_d + g_n * k / 2;
                                 auto aptr = A_d;
                                 auto cptr = C_d + g_n;
                                 float tmpAcc[TileK];
#pragma unroll
                                 for (int i = 0; i < TileK; i++) {
                                   tmpAcc[i] = 0.f;
                                 }
                                 int constexpr Unroll = 1;
                                 for (int i = 0; i < k; i += GroupK * Unroll) {
#pragma unroll
                                   for (int iu = 0; iu < Unroll; iu++) {
                                     float tmpf[TileK];
                                     uint8_t tmps8[TileK / 2];
                                     *(sycl::vec<uint8_t, TileK / 2>*)tmps8 =
                                         *(sycl::vec<uint8_t, TileK / 2>*)(bptr + sg_id * TileK / 2);
                                     auto scale = *(sptr + sg_id * TileK / blocksize);
#pragma unroll
                                     for (int ikk = 0; ikk < TileK; ikk += 2) {
                                       tmpf[ikk] = static_cast<int8_t>((tmps8[ikk / 2] & 0x0f) << 4) * scale;
                                       tmpf[ikk + 1] = static_cast<int8_t>((tmps8[ikk / 2] & 0xf0)) * scale;
                                     }
#pragma unroll
                                     for (int ikk = 0; ikk < TileK; ikk += 1) {
                                       tmpAcc[ikk] += aptr[sg_id * TileK + ikk] * tmpf[ikk];
                                     }
                                     sptr += GroupK / blocksize;
                                     aptr += GroupK;
                                     bptr += GroupK / 2;
                                   }
                                 }
                                 if constexpr (TileK >= 2) {
                                   for (int i = 0; i < TileK / 2; i++) {
                                     tmpAcc[i] += tmpAcc[i + TileK / 2];
                                   }
                                 }
                                 if constexpr (TileK >= 4) {
                                   for (int i = 0; i < TileK / 4; i++) {
                                     tmpAcc[i] += tmpAcc[i + TileK / 4];
                                   }
                                 }
                                 if constexpr (TileK >= 8) {
                                   for (int i = 0; i < TileK / 8; i++) {
                                     tmpAcc[i] += tmpAcc[i + TileK / 8];
                                   }
                                 }
                                 if constexpr (TileK >= 16) {
                                   for (int i = 0; i < TileK / 16; i++) {
                                     tmpAcc[i] += tmpAcc[i + TileK / 16];
                                   }
                                 }
                                 if constexpr (TileK >= 32) {
                                   for (int i = 0; i < TileK / 32; i++) {
                                     tmpAcc[i] += tmpAcc[i + TileK / 32];
                                   }
                                 }
                                 auto sum = 0.f;
                                 for (int i = 0; i < SgSize; i++) {
                                   sum += sg.shuffle(tmpAcc[0], i);
                                 }
                                 if (sg_id == 0) {
                                   *cptr = sum;
                                 }
                               });
        });
        e_esimd.wait();
        log.add(event_helper::execute_time(e_esimd) * 1000);
        if (tm.stop() >= timems) {
          break;
        }
      }
    }
    log.record();
    double flops = double(psize) / log.min_val / 1e6;
    printf(" %s Flops:%.3f\n", log.get_log_str(), flops);
  }

  template <typename LOG_T>
  void benchmark_gemv_T3(int m, int n, int k, int batch, AType* A, uint8_t* B, float* B_scale, CType* C, float timems) {
    LOG_T log;
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    utils::timer<std::chrono::milliseconds> tm;
    auto A_d = A;
    auto B_d = B;
    auto C_d = C;
    auto S_d = B_scale;
    auto psize = (size_t)m * n * k * 2;
    int constexpr blocksize = 32;
    int blks = k / blocksize;
    tm.start();
    while (tm.stop() < timems) {
      for (size_t i = 0; i < batch; i++) {
        int constexpr SgSize = 16;
        int constexpr TileK = 32;
        int constexpr KSlicing = 1;
        int constexpr GroupK = SgSize * TileK;
        int sg_ksize = k / KSlicing;
        sycl::range<1> group{KSlicing * SgSize};
        sycl::range<1> problem{n * KSlicing * SgSize};
        auto e_esimd = q->submit([&](sycl::handler& cgh) {
          sycl::local_accessor<float, 1> slm(KSlicing - 1, cgh);
          cgh.parallel_for(
              sycl::nd_range<1>(problem, group), [=](sycl::nd_item<1> it) [[intel::reqd_sub_group_size(SgSize)]] {
                int g_idx = it.get_group(0);
                auto sg = it.get_sub_group();
                int sg_id = sg.get_local_id()[0];
                int sg_group_id = sg.get_group_id()[0];
                int g_n = g_idx;
                int g_k = sg_group_id * GroupK;
                auto sptr = S_d + g_n * blks + g_k / blocksize;
                auto bptr = B_d + g_n * k / 2 + g_k / 2;
                auto aptr = A_d + g_k;
                auto cptr = C_d + g_n;
                float tmpAcc[TileK];
#pragma unroll
                for (int i = 0; i < TileK; i++) {
                  tmpAcc[i] = 0.f;
                }
                int constexpr Unroll = 1;
                for (int i = 0; i < sg_ksize; i += GroupK * Unroll) {
#pragma unroll
                  for (int ir = 0; ir < Unroll; ir++) {
                    float tmpf[TileK];
                    uint8_t tmps8[TileK / 2];
                    *(sycl::vec<float, TileK / 8>*)tmps8 = *(sycl::vec<float, TileK / 8>*)(bptr + sg_id * TileK / 2);
                    auto scale = *(sptr + sg_id * TileK / blocksize);
                    for (int ikk = 0; ikk < TileK; ikk += 2) {
                      tmpf[ikk] = static_cast<int8_t>((tmps8[ikk / 2] & 0x0f) << 4) * scale;
                      tmpf[ikk + 1] = static_cast<int8_t>((tmps8[ikk / 2] & 0xf0)) * scale;
                    }
                    for (int ikk = 0; ikk < TileK; ikk += 1) {
                      tmpAcc[ikk] += aptr[sg_id * TileK + ikk] * tmpf[ikk];
                    }
                    sptr += KSlicing * GroupK / blocksize;
                    aptr += KSlicing * GroupK;
                    bptr += KSlicing * GroupK / 2;
                  }
                }
                if constexpr (TileK >= 2) {
                  for (int i = 0; i < TileK / 2; i++) {
                    tmpAcc[i] += tmpAcc[i + TileK / 2];
                  }
                }
                if constexpr (TileK >= 4) {
                  for (int i = 0; i < TileK / 4; i++) {
                    tmpAcc[i] += tmpAcc[i + TileK / 4];
                  }
                }
                if constexpr (TileK >= 8) {
                  for (int i = 0; i < TileK / 8; i++) {
                    tmpAcc[i] += tmpAcc[i + TileK / 8];
                  }
                }
                if constexpr (TileK >= 16) {
                  for (int i = 0; i < TileK / 16; i++) {
                    tmpAcc[i] += tmpAcc[i + TileK / 16];
                  }
                }
                if constexpr (TileK >= 32) {
                  for (int i = 0; i < TileK / 32; i++) {
                    tmpAcc[i] += tmpAcc[i + TileK / 32];
                  }
                }
                auto sum = 0.f;
                for (int i = 0; i < SgSize; i++) {
                  sum += sg.shuffle(tmpAcc[0], i);
                }
                if constexpr (KSlicing > 1) {
                  if (sg_group_id != 0) {
                    slm[sg_group_id - 1] = sum;
                  }
                  it.barrier(sycl::access::fence_space::local_space);
                  if (sg_group_id == 0) {
                    for (int i = 0; i < KSlicing - 1; i++) {
                      sum += slm[i];
                    }
                    if (sg_id == 0) {
                      *cptr = sum;
                    }
                  }
                } else {
                  if (sg_id == 0) {
                    *cptr = sum;
                  }
                }
              });
        });
        e_esimd.wait();
        log.add(event_helper::execute_time(e_esimd) * 1000);
        if (tm.stop() >= timems) {
          break;
        }
      }
    }
    log.record();
    double flops = double(psize) / log.min_val / 1e6;
    printf(" %s Flops:%.3f\n", log.get_log_str(), flops);
  }

  template <typename LOG_T>
  void benchmark_gemv_T4(int m, int n, int k, int batch, AType* A, uint8_t* B, float* B_scale, CType* C, float timems) {
    LOG_T log;
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    utils::timer<std::chrono::milliseconds> tm;
    auto A_d = A;
    auto B_d = B;
    auto C_d = C;
    auto S_d = B_scale;
    auto psize = (size_t)m * n * k * 2;
    int constexpr blocksize = 32;
    int blks = k / blocksize;
    tm.start();
    using global_ptr = sycl::multi_ptr<float, sycl::access::address_space::global_space>;
    while (tm.stop() < timems) {
      for (size_t i = 0; i < batch; i++) {
        int constexpr SgSize = 16;
        int constexpr TileK = 32;
        int constexpr GroupK = SgSize * TileK;
        sycl::range<1> group{SgSize};
        sycl::range<1> problem{n * SgSize};
        auto e_esimd = q->submit([&](sycl::handler& cgh) {
          cgh.parallel_for(sycl::nd_range<1>(problem, group),
                           [=](sycl::nd_item<1> it) [[intel::reqd_sub_group_size(SgSize)]] {
                             int g_idx = it.get_group(0);
                             auto sg = it.get_sub_group();
                             int sg_id = sg.get_local_id()[0];
                             int g_n = g_idx;
                             auto sptr = S_d + g_n * blks;
                             auto bptr = B_d + g_n * k / 2;
                             auto aptr = A_d;
                             auto cptr = C_d + g_n;
                             float tmpAcc[TileK];
#pragma unroll
                             for (int i = 0; i < TileK; i++) {
                               tmpAcc[i] = 0.f;
                             }
                             for (int i = 0; i < k; i += GroupK) {
                               float tmpf[TileK];
                               // uint8_t tmps8[TileK / 2];
                               // *(sycl::vec<uint8_t, TileK / 2>*)tmps8 = *(sycl::vec<uint8_t, TileK / 2>*)(bptr +
                               // sg_id * TileK / 2);
                               auto scale = *(sptr + sg_id * TileK / blocksize);
#pragma unroll
                               for (int ikk = 0; ikk < TileK; ikk += 2) {
                                 auto tmps8 = *(bptr + sg_id * TileK / 2 + ikk / 2);
                                 tmpf[ikk] = static_cast<int8_t>((tmps8 & 0x0f) << 4) * scale;
                                 tmpf[ikk + 1] = static_cast<int8_t>((tmps8 & 0xf0)) * scale;
                               }
                               for (int ikk = 0; ikk < TileK; ikk += 1) {
                                 tmpAcc[ikk] += aptr[sg_id * TileK + ikk] * tmpf[ikk];
                               }
                               sptr += GroupK / blocksize;
                               aptr += GroupK;
                               bptr += GroupK / 2;
                             }
                             if constexpr (TileK >= 2) {
                               for (int i = 0; i < TileK / 2; i++) {
                                 tmpAcc[i] += tmpAcc[i + TileK / 2];
                               }
                             }
                             if constexpr (TileK >= 4) {
                               for (int i = 0; i < TileK / 4; i++) {
                                 tmpAcc[i] += tmpAcc[i + TileK / 4];
                               }
                             }
                             if constexpr (TileK >= 8) {
                               for (int i = 0; i < TileK / 8; i++) {
                                 tmpAcc[i] += tmpAcc[i + TileK / 8];
                               }
                             }
                             if constexpr (TileK >= 16) {
                               for (int i = 0; i < TileK / 16; i++) {
                                 tmpAcc[i] += tmpAcc[i + TileK / 16];
                               }
                             }
                             if constexpr (TileK >= 32) {
                               for (int i = 0; i < TileK / 32; i++) {
                                 tmpAcc[i] += tmpAcc[i + TileK / 32];
                               }
                             }
                             auto sum = 0.f;
                             for (int i = 0; i < SgSize; i++) {
                               sum += sg.shuffle(tmpAcc[0], i);
                             }
                             if (sg_id == 0) {
                               *cptr = sum;
                             }
                           });
        });
        e_esimd.wait();
        log.add(event_helper::execute_time(e_esimd) * 1000);
        if (tm.stop() >= timems) {
          break;
        }
      }
    }
    log.record();
    double flops = double(psize) / log.min_val / 1e6;
    printf(" %s Flops:%.3f\n", log.get_log_str(), flops);
  }

  void benchmark_all(int m, int n, int k) {
    auto memsize = gemm_memsize(m, n, k, BTLA_DTYPE::F32, BTLA_DTYPE::F32, BTLA_DTYPE::F32);
    auto batch = auto_batch(memsize);
    printf("%d %d %d %d %s %s %s\n", m, n, k, batch, bestla_dtype_str(BTLA_DTYPE::F32),
           bestla_dtype_str(BTLA_DTYPE::F32), bestla_dtype_str(BTLA_DTYPE::F32));
    avector<AType> A(size_t(m) * k * batch);
    avector<BType> B(size_t(k) * n * batch);
    avector<float> B_scale(size_t(k) * n * batch);
    avector<CType> C(size_t(m) * n * batch, 0);
    fill_buffer_randn(A.data(), m * k, -0.5f, 0.5f);
    fill_buffer_randn(B.data(), n * k, -0.5f, 0.5f);
    fill_buffer_randn(B_scale.data(), n * k, -0.5f, 0.5f);
    avector<uint8_t> B_s8(k * n * batch / 2);
    fill_buffer_randn(B_s8.data(), B_s8.size(), uint8_t(0), uint8_t(255));
    for (size_t i = 0; i < batch - 1; i++) {
      memcpy(A.data() + i * m * k, A.data(), m * k * sizeof(AType));
      memcpy(B.data() + i * n * k, B.data(), n * k * sizeof(BType));
      memcpy(B_s8.data() + i * n * k, B_s8.data(), n * k * sizeof(uint8_t) / 2);
      memcpy(B_scale.data() + i * n * k, B_scale.data(), n * k * sizeof(float));
    }
    using LOG = timer_statistics_logger<TestMs * 2>;
    float testtime = float(TestMs);
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    sycl_vector<float> dA(A.size(), q), dB(B.size(), q), dC(C.size(), q), dB_scale(B_scale.size(), q);
    sycl_vector<uint8_t> dBs8(B_s8.size(), q);
    q->memcpy(dA.data(), A.data(), A.size() * 4).wait();
    q->memcpy(dB.data(), B.data(), B.size() * 4).wait();
    if (m == 1) {
      benchmark_gemv_T3<LOG>(m, n, k, batch, dA.data(), dBs8.data(), dB_scale.data(), dC.data(), testtime);
    } else {
      benchmarkT<LOG>(m, n, k, batch, dA.data(), dBs8.data(), dB_scale.data(), dC.data(), testtime);
    }
  }
};
static Benchmark_S4Fp32Fp32 sBenchmark_S4Fp32Fp32;

class Benchmark_S4Fp16Fp16 {
 public:
  Benchmark_S4Fp16Fp16() {
    UT_START();
    benchmark_all(1, 4096, 4096);
    benchmark_all(1, 4096, 4096 * 4);
    benchmark_all(1, 4096 * 3, 4096);
  }

  using AType = sycl::half;
  using BType = sycl::half;
  using CType = sycl::half;

  template <typename LOG_T>
  void benchmark_gemv_T2(int m, int n, int k, int batch, AType* A, uint8_t* B, BType* B_scale, CType* C, float timems) {
    LOG_T log;
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    utils::timer<std::chrono::milliseconds> tm;
    auto A_d = (const AType*)A;
    auto B_d = B;
    auto C_d = C;
    auto S_d = B_scale;
    auto psize = (size_t)m * n * k * 2;
    int constexpr blocksize = 32;
    int blks = k / blocksize;
    tm.start();
    while (tm.stop() < timems) {
      for (size_t i = 0; i < batch; i++) {
        int constexpr SgSize = 16;
        int constexpr TileK = 32;
        int constexpr GroupK = SgSize * TileK;
        sycl::range<1> group{SgSize};
        sycl::range<1> problem{n * SgSize};
        auto e_esimd = q->submit([&](sycl::handler& cgh) {
          cgh.parallel_for(
              sycl::nd_range<1>(problem, group), [=](sycl::nd_item<1> it) [[intel::reqd_sub_group_size(SgSize)]] {
                int g_idx = it.get_group(0);
                auto sg = it.get_sub_group();
                int sg_id = sg.get_local_id()[0];
                int g_n = g_idx;
                auto sptr = S_d + g_n * blks;
                auto bptr = B_d + g_n * k / 2;
                auto aptr = A_d;
                auto cptr = C_d + g_n;
                sycl::half tmpAcc[TileK];
#pragma unroll
                for (int i = 0; i < TileK; i++) {
                  tmpAcc[i] = 0.f;
                }
                for (int i = 0; i < k; i += GroupK) {
                  sycl::half tmpf[TileK];
                  uint8_t tmps8[TileK / 2];
                  *(sycl::vec<uint8_t, TileK / 2>*)tmps8 = *(sycl::vec<uint8_t, TileK / 2>*)(bptr + sg_id * TileK / 2);
                  auto scale = *(sptr + sg_id * TileK / blocksize);
                  for (int ikk = 0; ikk < TileK; ikk += 2) {
                    tmpf[ikk] = static_cast<int8_t>((tmps8[ikk / 2] & 0x0f) << 4) * scale;
                    tmpf[ikk + 1] = static_cast<int8_t>((tmps8[ikk / 2] & 0xf0)) * scale;
                  }
                  for (int ikk = 0; ikk < TileK; ikk += 1) {
                    tmpAcc[ikk] += aptr[sg_id * TileK + ikk] * tmpf[ikk];
                  }
                  sptr += GroupK / blocksize;
                  aptr += GroupK;
                  bptr += GroupK / 2;
                }
                if constexpr (TileK >= 2) {
                  for (int i = 0; i < TileK / 2; i++) {
                    tmpAcc[i] += tmpAcc[i + TileK / 2];
                  }
                }
                if constexpr (TileK >= 4) {
                  for (int i = 0; i < TileK / 4; i++) {
                    tmpAcc[i] += tmpAcc[i + TileK / 4];
                  }
                }
                if constexpr (TileK >= 8) {
                  for (int i = 0; i < TileK / 8; i++) {
                    tmpAcc[i] += tmpAcc[i + TileK / 8];
                  }
                }
                if constexpr (TileK >= 16) {
                  for (int i = 0; i < TileK / 16; i++) {
                    tmpAcc[i] += tmpAcc[i + TileK / 16];
                  }
                }
                if constexpr (TileK >= 32) {
                  for (int i = 0; i < TileK / 32; i++) {
                    tmpAcc[i] += tmpAcc[i + TileK / 32];
                  }
                }
                auto sum = 0.f;
                for (int i = 0; i < 1; i++) {
                  sum += sg.shuffle(tmpAcc[0], i);
                }
                if (sg_id == 0) {
                  *cptr = sum;
                }
              });
        });
        e_esimd.wait();
        log.add(event_helper::execute_time(e_esimd) * 1000);
        if (tm.stop() >= timems) {
          break;
        }
      }
    }
    log.record();
    double flops = double(psize) / log.min_val / 1e6;
    printf(" %s Flops:%.3f\n", log.get_log_str(), flops);
  }

  void benchmark_all(int m, int n, int k) {
    auto memsize = gemm_memsize(m, n, k, BTLA_DTYPE::F16, BTLA_DTYPE::F16, BTLA_DTYPE::F16);
    auto batch = auto_batch(memsize);
    printf("%d %d %d %d %s %s %s\n", m, n, k, batch, bestla_dtype_str(BTLA_DTYPE::F16),
           bestla_dtype_str(BTLA_DTYPE::F16), bestla_dtype_str(BTLA_DTYPE::F16));
    avector<AType> A(size_t(m) * k * batch);
    avector<BType> B(size_t(k) * n * batch);
    avector<BType> B_scale(size_t(k) * n * batch);
    avector<CType> C(size_t(m) * n * batch, 0);
    // fill_buffer_randn(A.data(), m * k, -0.5f, 0.5f);
    // fill_buffer_randn(B.data(), n * k, -0.5f, 0.5f);
    // fill_buffer_randn(B_scale.data(), n * k, -0.5f, 0.5f);
    avector<uint8_t> B_s8(k * n * batch / 2);
    fill_buffer_randn(B_s8.data(), B_s8.size(), uint8_t(0), uint8_t(255));
    for (size_t i = 0; i < batch - 1; i++) {
      memcpy(A.data() + i * m * k, A.data(), m * k * sizeof(AType));
      memcpy(B.data() + i * n * k, B.data(), n * k * sizeof(BType));
      memcpy(B_s8.data() + i * n * k / 2, B_s8.data(), n * k * sizeof(uint8_t) / 2);
      memcpy(B_scale.data() + i * n * k, B_scale.data(), n * k * sizeof(BType));
    }
    using LOG = timer_statistics_logger<TestMs * 2>;
    float testtime = float(TestMs);
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    sycl_vector<BType> dA(A.size(), q), dB(B.size(), q), dC(C.size(), q), dB_scale(B_scale.size(), q);
    sycl_vector<uint8_t> dBs8(B_s8.size(), q);
    q->memcpy(dA.data(), A.data(), A.size() * 2).wait();
    q->memcpy(dB.data(), B.data(), B.size() * 2).wait();
    benchmark_gemv_T2<LOG>(m, n, k, batch, dA.data(), dBs8.data(), dB_scale.data(), dC.data(), testtime);
  }
};
// static Benchmark_S4Fp16Fp16 sBenchmark_S4Fp16Fp16;

class Benchmark_DequantS4 {
 public:
  Benchmark_DequantS4() {
    UT_START();
    benchmark_all_reorder(4096, 4096, 32);
    benchmark_all(4096, 4096, 32);
    benchmark_all(16384, 4096, 32);
    benchmark_all(16384, 16384, 32);
    benchmark_memcpy(2480, 4096, 32);
    benchmark_memcpy(16384, 16384, 32);
  }

  void benchmark_all_reorder(int n, int k, int blocksize) {
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    printf("Test Case %s: %d %d %d Device:%s\n", __FUNCTION__, n, k, blocksize, dev->getName().c_str());
    avector<uint8_t> rawB(k * n / 2);
    int blks = updiv(k, blocksize);
    avector<float> scale(blks * n), dequant(n * k), ref(n * k);
    fill_buffer_randn(scale.data(), scale.size(), 0.01f, 0.03f);
    fill_buffer_randn(rawB.data(), rawB.size(), uint8_t(0), uint8_t(255));
    auto srcptr = (utils::int4x2*)rawB.data();
    for (int j = 0; j < n; j += 1) {
      for (int i = 0; i < k; i += 2) {
        auto tmp = srcptr[i / 2 + j * k / 2];
        auto noffset = i / blocksize + j * blks;
        ref[i + j * k] = static_cast<float>(static_cast<int8_t>(tmp.x) << 4) * scale[noffset];
        ref[i + 1 + j * k] = static_cast<float>(static_cast<int8_t>(tmp.y) << 4) * scale[noffset];
      }
    }
    sycl_vector<float> dS(scale.size(), q), dequantB(n * k, q);
    sycl_vector<uint8_t> dB(rawB.size(), q);
    q->memcpy(dS.data(), scale.data(), scale.size() * 4).wait();
    q->memcpy(dB.data(), rawB.data(), rawB.size() * 1).wait();
    int constexpr SgSize = 16;
    int constexpr TileK = 2;
    int constexpr TileN = 1;
    int constexpr GroupN = TileN;
    int constexpr GroupK = SgSize * TileK;
    sycl::range<1> group{SgSize};
    sycl::range<1> problem{n * blks * SgSize};
    auto S_d = dS.data();
    auto B_d = dB.data();
    auto DB_d = dequantB.data();
    auto n_blks = updiv(n, SgSize);

    auto deq_kernel = [&](sycl::handler& cgh) {
      cgh.parallel_for(sycl::nd_range<1>(problem, group),
                       [=](sycl::nd_item<1> it) [[intel::reqd_sub_group_size(SgSize)]] {
                         int g_idx = it.get_group(0);
                         auto sg = it.get_sub_group();
                         int sg_id = sg.get_local_id()[0];
                         int g_idx_k = g_idx % blks;
                         int g_idx_n = g_idx / blks;
                         int g_n = g_idx_n * GroupN;
                         int g_k = g_idx_k * blocksize;
                         auto sptr = S_d + g_idx_k + g_n * blks;
                         auto bptr = B_d + (g_k + g_n * k) / 2;
                         auto dbptr = DB_d + g_k + g_n * k;
                         float scale = *sptr;

                         int constexpr UnrollK = GroupK;
                         for (int ik = 0; ik < blocksize; ik += UnrollK) {
                           uint8_t tmp = *(bptr + ik / 2 + sg_id);
                           static_assert(TileK == 2);
                           float tmpf[TileK];
                           tmpf[0] = static_cast<int8_t>((tmp & 0x0f) << 4) * scale;
                           tmpf[1] = static_cast<int8_t>((tmp & 0xf0)) * scale;
                           for (int ikk = 0; ikk < TileK; ikk++) {
                             dbptr[sg_id * TileK + ikk + ik] = tmpf[ikk];
                           }
                         }
                       });
    };

    using LOG = timer_statistics_logger<TestMs * 2>;
    LOG log;
    utils::timer<std::chrono::milliseconds> tm;
    tm.start();
    while (tm.stop() < TestMs) {
      for (size_t i = 0; i < 1; i++) {
        auto e_esimd = q->submit(deq_kernel);
        log.add(event_helper::execute_time(e_esimd) * 1000);
        if (tm.stop() >= TestMs) {
          break;
        }
      }
    }

    q->memcpy(dequant.data(), DB_d, dequant.size() * 4).wait();
    buffer_error(ref.data(), dequant.data(), dequant.size(), 0.001f);
    log.record();
    auto psize = (size_t)n * k * 4 + n * k / 2 + n * k / blocksize * 4;
    double flops = double(psize) / log.min_val / 1e6;
    printf(" %s Memory Bandwidth:%.3f\n", log.get_log_str(), flops);
  }

  void benchmark_all(int n, int k, int blocksize) {
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    printf("Test Case %s: %d %d %d Device:%s\n", __FUNCTION__, n, k, blocksize, dev->getName().c_str());
    avector<uint8_t> rawB(k * n / 2);
    int blks = updiv(k, blocksize);
    avector<float> scale(blks * n), dequant(n * k), ref(n * k);
    fill_buffer_randn(scale.data(), scale.size(), 0.01f, 0.03f);
    fill_buffer_randn(rawB.data(), rawB.size(), uint8_t(0), uint8_t(255));
    auto srcptr = (utils::int4x2*)rawB.data();
    for (int i = 0; i < k; i++) {
      for (int j = 0; j < n; j += 2) {
        auto tmp = srcptr[i * n / 2 + j / 2];
        auto noffset = i / blocksize * n + j;
        ref[i * n + j + 0] = static_cast<float>(static_cast<int8_t>(tmp.x) << 4) * scale[noffset + 0];
        ref[i * n + j + 1] = static_cast<float>(static_cast<int8_t>(tmp.y) << 4) * scale[noffset + 1];
      }
    }
    sycl_vector<float> dS(scale.size(), q), dequantB(n * k, q);
    sycl_vector<uint8_t> dB(rawB.size(), q);
    q->memcpy(dS.data(), scale.data(), scale.size() * 4).wait();
    q->memcpy(dB.data(), rawB.data(), rawB.size() * 1).wait();
    int constexpr SgSize = 16;
    int constexpr TileN = 2;
    int constexpr GroupN = SgSize * TileN;
    sycl::range<1> group{SgSize};
    sycl::range<1> problem{n / TileN * blks};
    auto S_d = dS.data();
    auto B_d = dB.data();
    auto DB_d = dequantB.data();
    auto n_blks = updiv(n / TileN, SgSize);

    auto deq_kernel = [&](sycl::handler& cgh) {
      cgh.parallel_for(sycl::nd_range<1>(problem, group),
                       [=](sycl::nd_item<1> it) [[intel::reqd_sub_group_size(SgSize)]] {
                         int g_idx = it.get_group(0);
                         auto sg = it.get_sub_group();
                         int sg_id = sg.get_local_id()[0];
                         int g_idx_n = g_idx % n_blks;
                         int g_idx_k = g_idx / n_blks;
                         int g_n = g_idx_n * GroupN;
                         int g_k = g_idx_k * blocksize;
                         auto sptr = S_d + g_idx_k * n + g_n;
                         auto bptr = B_d + (g_k * n + g_n) / 2;
                         auto dbptr = DB_d + g_k * n + g_n;
                         float scale[TileN];
#pragma unroll
                         for (int in = 0; in < TileN; in++) {
                           scale[in] = *(sptr + sg_id * TileN + in);
                         }
                         int constexpr UnrollK = 8;
                         for (int ik = 0; ik < blocksize; ik += UnrollK) {
#pragma unroll
                           for (int ikk = 0; ikk < UnrollK; ikk++) {
                             uint8_t tmp = *(bptr + (ik + ikk) * n / 2 + sg_id * TileN / 2);
                             float tmpf[TileN];
                             tmpf[0] = static_cast<int8_t>((tmp & 0x0f) << 4) * scale[0];
                             tmpf[1] = static_cast<int8_t>((tmp & 0xf0)) * scale[1];
            /*tmp = *(bptr + (ik + ikk) * n / 2 + sg_id * TileN / 2 + 1);
            tmpf[2] = static_cast<int8_t>((tmp & 0x0f) << 4) * scale[2];
            tmpf[3] = static_cast<int8_t>((tmp & 0xf0)) * scale[3];*/
#pragma unroll
                             for (int in = 0; in < TileN; in++) {
                               dbptr[in + sg_id * TileN + (ik + ikk) * n] = tmpf[in];
                             }
                           }
                         }
                       });
    };

    using LOG = timer_statistics_logger<TestMs * 2>;
    LOG log;
    utils::timer<std::chrono::milliseconds> tm;
    tm.start();
    while (tm.stop() < TestMs) {
      for (size_t i = 0; i < 1; i++) {
        auto e_esimd = q->submit(deq_kernel);
        e_esimd.wait();
        log.add(event_helper::execute_time(e_esimd) * 1000);
        if (tm.stop() >= TestMs) {
          break;
        }
      }
    }

    q->memcpy(dequant.data(), DB_d, dequant.size() * 4).wait();
    buffer_error(ref.data(), dequant.data(), dequant.size(), 0.001f);
    log.record();
    auto psize = (size_t)n * k * 4 + n * k / 2 + n * k / blocksize * 4;
    double flops = double(psize) / log.min_val / 1e6;
    printf(" %s Memory Bandwidth:%.3f\n", log.get_log_str(), flops);
  }

  void benchmark_memcpy(int n, int k, int blocksize) {
    auto dev = UT_Device::get();
    auto q = dev->getQueue();
    printf("Test Case %s: %d %d %d Device:%s\n", __FUNCTION__, n, k, blocksize, dev->getName().c_str());
    avector<float> dequant(n * k);
    fill_buffer_randn(dequant.data(), dequant.size(), 0.01f, 0.03f);
    sycl_vector<float> dequantB0(n * k, q);
    sycl_vector<float> dequantB1(n * k, q);
    q->memcpy(dequantB0.data(), dequant.data(), dequant.size() * 4).wait();

    using LOG = timer_statistics_logger<TestMs * 2>;
    LOG log;
    utils::timer<std::chrono::milliseconds> tm;
    tm.start();
    while (tm.stop() < TestMs) {
      for (size_t i = 0; i < 1; i++) {
        auto e = q->memcpy(dequantB1.data(), dequantB0.data(), dequantB0.size() * 4);
        e.wait();
        log.add(event_helper::execute_time(e) * 1000);
        if (tm.stop() >= TestMs) {
          break;
        }
      }
    }

    log.record();
    auto psize = (size_t)n * k * 4 * 2;
    double flops = double(psize) / log.min_val / 1e6;
    printf(" %s Memory Bandwidth:%.3f\n", log.get_log_str(), flops);
  }
};
// static Benchmark_DequantS4 sBenchmark_DequantS4;
}  // namespace sycl_ut
}  // namespace bestla
