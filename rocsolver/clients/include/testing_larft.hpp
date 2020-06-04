/* ************************************************************************
 * Copyright 2020 Advanced Micro Devices, Inc.
 * ************************************************************************ */

#include "norm.hpp"
#include "rocsolver_test.hpp"
#include "rocsolver_arguments.hpp"
#include "rocsolver.hpp"
#include "cblas_interface.h"
#include "clientcommon.hpp"

template <typename T>
void larft_checkBadArgs(const rocblas_handle handle,
                         const rocblas_direct direct,
                         const rocblas_storev storev,
                         const rocblas_int n, 
                         const rocblas_int k, 
                         T dV, 
                         const rocblas_int ldv,
                         T dt,
                         T dT,
                         const rocblas_int ldt)
{
    // handle
    EXPECT_ROCBLAS_STATUS(rocsolver_larft(nullptr,direct,storev,n,k,dV,ldv,dt,dT,ldt),
                          rocblas_status_invalid_handle); 

    // values
    EXPECT_ROCBLAS_STATUS(rocsolver_larft(handle,rocblas_direct(-1),storev,n,k,dV,ldv,dt,dT,ldt),
                          rocblas_status_invalid_value); 
    EXPECT_ROCBLAS_STATUS(rocsolver_larft(handle,direct,rocblas_storev(-1),n,k,dV,ldv,dt,dT,ldt),
                          rocblas_status_invalid_value); 
    
    // pointers
    EXPECT_ROCBLAS_STATUS(rocsolver_larft(handle,direct,storev,n,k,(T)nullptr,ldv,dt,dT,ldt),
                          rocblas_status_invalid_pointer);
    EXPECT_ROCBLAS_STATUS(rocsolver_larft(handle,direct,storev,n,k,dV,ldv,(T)nullptr,dT,ldt),
                          rocblas_status_invalid_pointer);
    EXPECT_ROCBLAS_STATUS(rocsolver_larft(handle,direct,storev,n,k,dV,ldv,dt,(T)nullptr,ldt),
                          rocblas_status_invalid_pointer);

    // quick return with invalid pointers
    EXPECT_ROCBLAS_STATUS(rocsolver_larft(handle,direct,storev,0,k,(T)nullptr,ldv,dt,dT,ldt),
                          rocblas_status_success);
}

template <typename T>
void testing_larft_bad_arg()
{
    // safe arguments
    rocblas_local_handle handle;  
    rocblas_direct direct = rocblas_forward_direction;
    rocblas_storev storev = rocblas_column_wise;
    rocblas_int k = 1;
    rocblas_int n = 1;
    rocblas_int ldv = 1;
    rocblas_int ldt = 1;

    // memory allocation
    device_strided_batch_vector<T> dV(1,1,1,1);
    device_strided_batch_vector<T> dt(1,1,1,1);
    device_strided_batch_vector<T> dT(1,1,1,1);
    CHECK_HIP_ERROR(dV.memcheck());
    CHECK_HIP_ERROR(dT.memcheck());
    CHECK_HIP_ERROR(dt.memcheck());

    // check bad arguments
    larft_checkBadArgs(handle,direct,storev,n,k,dV.data(),ldv,dt.data(),dT.data(),ldt);
}   


template <typename T, typename Td, typename Th> 
void larft_getError(const rocblas_handle handle,
                         const rocblas_direct direct,
                         const rocblas_storev storev,
                         const rocblas_int n, 
                         const rocblas_int k, 
                         Td &dV, 
                         const rocblas_int ldv,
                         Td &dt,
                         Td &dT,
                         const rocblas_int ldt,
                         Th &hV,
                         Th &ht,
                         Th &hT,
                         Th &hTr,
                         double *max_err)
{
    size_t size_w = size_t(k); 
    std::vector<T> hw(size_w);

    //initialize data 
    rocblas_init<T>(hV, true);

    // scale to avoid singularities
    // and create householder reflectors
    if (storev == rocblas_column_wise) {
        for (int j=0;j<k;++j) {
            for (int i=0;i<n;++i) {
                if (i == j)
                    hV[0][i+j*ldv] += 400;
                else
                    hV[0][i+j*ldv] -= 4;
            }
        }
        cblas_geqrf<T>(n, k, hV[0], ldv, ht[0], hw.data(), k);
    } else {
        for (int j=0;j<n;++j) {
            for (int i=0;i<k;++i) {
                if (i == j)
                    hV[0][i+j*ldv] += 400;
                else
                    hV[0][i+j*ldv] -= 4;
            }
        }
        cblas_gelqf<T>(k, n, hV[0], ldv, ht[0], hw.data(), k);
    }     
    
    // copy data from CPU to device
    CHECK_HIP_ERROR(dV.transfer_from(hV));
    CHECK_HIP_ERROR(dt.transfer_from(ht));

    // execute computations
    //GPU lapack
    CHECK_ROCBLAS_ERROR(rocsolver_larft(handle,direct,storev,n,k,dV.data(),ldv,dt.data(),dT.data(),ldt));
    CHECK_HIP_ERROR(hTr.transfer_from(dT));

    //CPU lapack
    cblas_larft<T>(direct,storev,n,k,hV[0],ldv,ht[0],hT[0],ldt);

    // error is ||hT - hTr|| / ||hT|| 
    // (THIS DOES NOT ACCOUNT FOR NUMERICAL REPRODUCIBILITY ISSUES. 
    // IT MIGHT BE REVISITED IN THE FUTURE)
    // using frobenius norm 
    *max_err = (direct == rocblas_forward_direction) ?
                    norm_error_upperTr('F',k,k,ldt,hT[0],hTr[0]):
                    norm_error_lowerTr('F',k,k,ldt,hT[0],hTr[0]);
}


template <typename T, typename Td, typename Th> 
void larft_getPerfData(const rocblas_handle handle,
                         const rocblas_direct direct,
                         const rocblas_storev storev,
                         const rocblas_int n, 
                         const rocblas_int k, 
                         Td &dV, 
                         const rocblas_int ldv,
                         Td &dt,
                         Td &dT,
                         const rocblas_int ldt,
                         Th &hV,
                         Th &ht,
                         Th &hT,
                         double *gpu_time_used,
                         double *cpu_time_used,
                         const rocblas_int hot_calls)
{
    // cpu-lapack performance
    *cpu_time_used = get_time_us();
    cblas_larft<T>(direct,storev,n,k,hV[0],ldv,ht[0],hT[0],ldt);
    *cpu_time_used = get_time_us() - *cpu_time_used;
        
    // cold calls    
    for(int iter = 0; iter < 2; iter++)
        CHECK_ROCBLAS_ERROR(rocsolver_larft(handle,direct,storev,n,k,dV.data(),ldv,dt.data(),dT.data(),ldt));

    // gpu-lapack performance
    *gpu_time_used = get_time_us();
    for(int iter = 0; iter < hot_calls; iter++)
        rocsolver_larft(handle,direct,storev,n,k,dV.data(),ldv,dt.data(),dT.data(),ldt);
    *gpu_time_used = (get_time_us() - *gpu_time_used) / hot_calls;       
}


template <typename T> 
void testing_larft(Arguments argus) 
{
    // get arguments 
    rocblas_local_handle handle;  
    rocblas_int k = argus.K;
    rocblas_int n = argus.N;
    rocblas_int ldv = argus.ldv; 
    rocblas_int ldt = argus.ldt; 
    rocblas_int hot_calls = argus.iters;
    char directC = argus.direct_option;
    char storevC = argus.storev;
    rocblas_direct direct = char2rocblas_direct(directC);
    rocblas_storev storev = char2rocblas_storev(storevC);
    
    // check non-supported values 
    // N/A

    // determine sizes
    bool row = (storev == rocblas_row_wise);
    size_t size_T = size_t(ldt) * k;
    size_t size_tau = size_t(k);
    size_t size_V = row ? size_t(ldv)*n : size_t(ldv)*k;
    double max_error = 0, gpu_time_used = 0, cpu_time_used = 0;

    // check invalid sizes
    bool invalid_size = (n < 0 || k < 1 || ldt < k || (row && ldv < k) || (!row && ldv < n));
    if (invalid_size) {
        EXPECT_ROCBLAS_STATUS(rocsolver_larft(handle,direct,storev,n,k,(T*)nullptr,ldv,(T*)nullptr,(T*)nullptr,ldt),
                              rocblas_status_invalid_size);

        if (argus.timing)  
            ROCSOLVER_BENCH_INFORM(1);

        return;
    }             

    // memory allocations
    host_strided_batch_vector<T> hT(size_T,1,size_T,1);
    host_strided_batch_vector<T> hTr(size_T,1,size_T,1);
    host_strided_batch_vector<T> ht(size_tau,1,size_tau,1);
    host_strided_batch_vector<T> hV(size_V,1,size_V,1);
    device_strided_batch_vector<T> dT(size_T,1,size_T,1);
    device_strided_batch_vector<T> dt(size_tau,1,size_tau,1);
    device_strided_batch_vector<T> dV(size_V,1,size_V,1);
    if (size_V) CHECK_HIP_ERROR(dV.memcheck());
    if (size_T) CHECK_HIP_ERROR(dT.memcheck());
    if (size_tau) CHECK_HIP_ERROR(dt.memcheck());
    
    // check quick return
    if (n == 0) {
        EXPECT_ROCBLAS_STATUS(rocsolver_larft(handle,direct,storev,n,k,dV.data(),ldv,dt.data(),dT.data(),ldt),
                              rocblas_status_success);

        if (argus.timing)  
            ROCSOLVER_BENCH_INFORM(0);
        
        return;
    }

    // check computations
    if (argus.unit_check || argus.norm_check)
        larft_getError<T>(handle, direct, storev, n, k, dV, ldv, dt, dT, ldt,
                         hV, ht, hT, hTr, &max_error); 

    // collect performance data 
    if (argus.timing) 
        larft_getPerfData<T>(handle, direct, storev, n, k, dV, ldv, dt, dT, ldt,
                          hV, ht, hT, &gpu_time_used, &cpu_time_used, hot_calls); 
        
    // validate results for rocsolver-test
    // using n * machine_precision as tolerance
    if (argus.unit_check) 
        rocsolver_test_check<T>(max_error,n);     

    // output results for rocsolver-bench
    if (argus.timing) {
        rocblas_cout << "\n============================================\n";
        rocblas_cout << "Arguments:\n";
        rocblas_cout << "============================================\n";
        rocsolver_bench_output("direct", "storev", "n", "k", "ldv", "ldt");
        rocsolver_bench_output(directC, storevC, n, k, ldv, ldt);

        rocblas_cout << "\n============================================\n";
        rocblas_cout << "Results:\n";
        rocblas_cout << "============================================\n";
        if (argus.norm_check) {
            rocsolver_bench_output("cpu_time", "gpu_time", "error");
            rocsolver_bench_output(cpu_time_used, gpu_time_used, max_error);
        }
        else {
            rocsolver_bench_output("cpu_time", "gpu_time");
            rocsolver_bench_output(cpu_time_used, gpu_time_used);
        }
        rocblas_cout << std::endl;
    }
}

#undef ERROR_EPS_MULTIPLIER