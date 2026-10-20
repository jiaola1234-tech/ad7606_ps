#include "jacobi_hls.h"
#include <iostream>
#include <cmath>

using namespace std;

int main() {
    // 创建8x8复数矩阵
    data_t A_in[8][8] = {
        {data_t(2.0f, 0.0f), data_t(1.0f, 0.5f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f),
         data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f)},
        {data_t(1.0f, -0.5f), data_t(3.0f, 0.0f), data_t(1.0f, 0.3f), data_t(0.0f, 0.0f),
         data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f)},
        {data_t(0.0f, 0.0f), data_t(1.0f, -0.3f), data_t(4.0f, 0.0f), data_t(1.0f, 0.2f),
         data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f)},
        {data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(1.0f, -0.2f), data_t(5.0f, 0.0f),
         data_t(1.0f, 0.1f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f)},
        {data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(1.0f, -0.1f),
         data_t(6.0f, 0.0f), data_t(1.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f)},
        {data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f),
         data_t(1.0f, 0.0f), data_t(7.0f, 0.0f), data_t(1.0f, -0.1f), data_t(0.0f, 0.0f)},
        {data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f),
         data_t(0.0f, 0.0f), data_t(1.0f, 0.1f), data_t(8.0f, 0.0f), data_t(1.0f, 0.0f)},
        {data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(0.0f, 0.0f),
         data_t(0.0f, 0.0f), data_t(0.0f, 0.0f), data_t(1.0f, 0.0f), data_t(9.0f, 0.0f)}
    };

    data_t eig_vals[8];
    data_t eig_vecs[8][8];

    // 调用Jacobi算法
    jacobi_hls((data_t (*)[8])A_in, (data_t *)eig_vals, (data_t (*)[8])eig_vecs);

    // 输出完整的复数特征值
    cout << "Eigenvalues (complex):" << endl;
    for (int i = 0; i < 8; i++) {
        cout << "λ" << i+1 << " = " << eig_vals[i].real << " + " << eig_vals[i].imag << "i" << endl;
    }

    // 输出完整的复数特征向量
    cout << "\nEigenvectors (first column complex):" << endl;
    for (int i = 0; i < 8; i++) {
        cout << "v" << i+1 << " = " << eig_vecs[i][0].real << " + " << eig_vecs[i][0].imag << "i" << endl;
    }

    return 0;
}