/*
  17.14 fixed point implementation using a 32 bit signed int.
  +---+----------- --------+------ ------+
  |   |           ~        |      ~      |
  +---+----------- --------+------ ------+
  31  30                   13            0
   sign    before point      after point
   bit
*/

/* 1 in 17.14 format. */
#define F (1 << 14)  


/* n denotes integer, x and y denote FP. */
int int_to_fp (int n); /* Convert integer n to fixed point. */
int fp_to_int (int x); /* Convert FP to int(round down) */
int fp_to_int_round (int x); /* Convert FP x to int(round). */
int add_fp (int x, int y); /* FP + FP */
int sub_fp (int x, int y); /* FP - FP */
int add_fp_int (int x, int n); /* FP + int */
int sub_fp_int (int x, int n); /* FP - int */
int mult_fp (int x, int y); /* FP * FP */
int mult_fp_int (int x, int y); /* FP * int */
int div_fp (int x, int y); /* FP / FP */
int div_fp_int (int x, int n); /* FP / int */

int 
int_to_fp (int n)
{
  return n * F;
}

int
fp_to_int (int x)
{
  return x / F;
}

int 
fp_to_int_round (int x)
{
  return x >= 0 ? (x + F / 2) / F : (x - F / 2) / F;
}

int 
add_fp (int x, int y) 
{
  return x + y;
}

int 
sub_fp (int x, int y)
{
  return x - y;
}

int
add_fp_int (int x, int n)
{
  return x + int_to_fp (n);
}

int
sub_fp_int (int x, int n)
{
  return x - int_to_fp (n);
}

int
mult_fp (int x, int y)
{
  return ((int64_t)x) * y / F;
}

int
mult_fp_int (int x, int n)
{
  return x * n;
}

int
div_fp (int x, int y)
{
  return ((int64_t)x) * F / y;
}

int
div_fp_int (int x, int n)
{
  return x / n;
}