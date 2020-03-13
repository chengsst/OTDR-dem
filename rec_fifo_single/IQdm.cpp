#include <stdio.h>

#include <math.h>

#include<vector>

#define ORDER 2 /* ORDER is the number of element a[] or b[] minus 1;*/ 
#define NP 11/* NP is the number of output or input filter minus 1;*/ 

#define PI 3.1415926f
using std::vector;





/*
********************************************************************************************************
fir1

********************************************************************************************************
*/






vector <double> fir1(int N, double w) //N=256,hamming,N为阶数256，滤波器长度是N+1=257
{
	int a = N / 2; //这里的N是阶数，是长度-1
	vector <int> n;
	int i0;
	for (i0 = 0; i0 <= N; i0++)
	{
		n.push_back(i0);
	}

	int len_n = n.size();
	int i1;
	vector <double> m;
	for (i1 = 0; i1 < len_n; i1++)
	{
		double eps = 2.2204e-16;
		m.push_back(n[i1] - a + eps);
	}
	int len_m = m.size();

	vector <double> h;//ideal理想滤波器
	int i2;
	for (i2 = 0; i2 < len_m; i2++)
	{
		h.push_back(sin(w * m[i2]) / (PI * m[i2]));
	}

	vector <double> B;//hamming窗系数
	int i3;
	for (i3 = 0; i3 < N; i3++)
	{
		B.push_back(0.54 - 0.46*cos(2.0 * PI * i3 / (N - 1)));
	}

	//hd = h.* B
	vector <double> hd;
	int i4;
	for (i4 = 0; i4 < N; i4++)
	{
		hd.push_back(h[i4] * B[i4]);
	}

	return hd;

}



/*
*************************************************************************
filter
Y = filter(B,A,X)
a(1)*y(n) = b(1)*x(n) + b(2)*x(n-1) + ... + b(nb+1)*x(n-nb)
- a(2)*y(n-1) - ... - a(na+1)*y(n-na)
this program is corresponding to command "y = filter(b,1,x)" in matlab

*************************************************************************
*/

void filter(int ord, float *a, float *b, int np, float *x, float *y)
{
	int i, j;
	y[0] = b[0] * x[0];
	for (i = 1; i<ord + 1; i++)
	{
		y[i] = 0.0;
		for (j = 0; j<i + 1; j++)
			y[i] = y[i] + b[j] * x[i - j];
		for (j = 0; j<i; j++)
			y[i] = y[i] - a[j + 1] * y[i - j - 1];
	}
	/* end of initial part */
	for (i = ord + 1; i<np + 1; i++)
	{
		y[i] = 0.0;
		for (j = 0; j<ord + 1; j++)
			y[i] = y[i] + b[j] * x[i - j];
		for (j = 0; j<ord; j++)
			y[i] = y[i] - a[j + 1] * y[i - j - 1];
	}
	return;
} /* end of filter */