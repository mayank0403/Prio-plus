#include <iostream>

#include <gmpxx.h>

#include "../circuit.h"
#include "../constants.h"
#include "../share.h"
#include "../client.h"
#include "../server.h"

extern "C" {
  #include "flint/flint.h"
  #include "flint/fmpz.h"
};

void test_CheckVar() {
  std::cout << "Testing CheckVar Eval and share_polynomials" << std::endl;
  Circuit* var_circuit = CheckVar();  // x^2 == y

  fmpz_t inp[2];
  fmpz_init(inp[0]);
  fmpz_init(inp[1]);

  fmpz_set_si(inp[0], 9);  // Set first input to 9
  fmpz_set_si(inp[1], 81);  // Set second to 81

  // Eval to verify 9^2 = 81
  /*
  Gate 0: input gate, x = 9
  Gate 1: input gate, y = 81
  Gate 2: mult gate, x^2 = 81
  Gate 3: negation gate, "-y" mod Int_Modulus
  Gate 4: addition gate, x^2 - y = 0, checked to be 0
  */
  bool eval = var_circuit->Eval(inp);
  std::cout << "Eval: " << eval << std::endl;

  // One mult gate (#2). Next power of 2 is 2 (N).
  ClientPacket* p0 = new ClientPacket(var_circuit->N(), var_circuit->NumMulInpGates());
  ClientPacket* p1 = new ClientPacket(var_circuit->N(), var_circuit->NumMulInpGates());
  share_polynomials(var_circuit, p0, p1);
  delete var_circuit;

  std::cout << "p0" << std::endl;
  p0->print();

  std::cout << "p1" << std::endl;
  p1->print();

  std::cout << "------ Running through validity checks" << std::endl;

  fmpz_t randomX;
  fmpz_init(randomX);
  fmpz_randm(randomX, seed, Int_Modulus);
  std::cout << "Random X: "; fmpz_print(randomX); std::cout << std::endl;

  Circuit* var_circuit0 = CheckVar();
  Checker* checker_0 = new Checker(var_circuit0, 0, p0);
  CheckerPreComp* pre0 = new CheckerPreComp(var_circuit0->N());
  pre0->setCheckerPrecomp(randomX);

  std::cout << "-=-=-=-=-=-" << std::endl;

  Circuit* var_circuit1 = CheckVar();
  Checker* checker_1 = new Checker(var_circuit1, 1, p1);
  CheckerPreComp* pre1 = new CheckerPreComp(var_circuit1->N());
  pre1->setCheckerPrecomp(randomX);

  auto corshare0 = checker_0->CorShareFn(pre0);
  auto corshare1 = checker_1->CorShareFn(pre1);

  Cor* cor0 = new Cor(corshare0, corshare1);
  Cor* cor1 = new Cor(corshare0, corshare1);

  fmpz_t out0, out1;
  fmpz_init(out0);
  fmpz_init(out1);

  checker_0->OutShare(out0,cor0);
  checker_1->OutShare(out1,cor1);

  bool result0 = AddToZero(out0, out1);
  bool result1 = AddToZero(out0, out1);

  std::cout << "out0 : "; fmpz_print(out0); std::cout << ", out1 : "; fmpz_print(out1); std::cout << std::endl;

  std::cout << "Result0 : " << std::boolalpha << result0 << std::endl;
  std::cout << "Result1 : " << std::boolalpha << result1 << std::endl;

  std::cout << "^v^v^ Shared validation: " << std::endl;
  fmpz_t tmp, rgr;
  fmpz_init(rgr);
  fmpz_init(tmp);
  fmpz_add(tmp, checker_0->evalF, checker_1->evalF);
  fmpz_mod(tmp, tmp, Int_Modulus);
  std::cout << "f(r) = "; fmpz_print(tmp); std::cout << std::endl;
  fmpz_add(rgr, checker_0->evalG, checker_1->evalG);
  fmpz_mod(rgr, rgr, Int_Modulus);
  std::cout << "r * g(r) = "; fmpz_print(rgr); std::cout << std::endl;
  fmpz_mul(tmp, tmp, rgr);
  fmpz_mod(tmp, tmp, Int_Modulus);
  std::cout << "r * f(r) * g(r) = "; fmpz_print(tmp); std::cout << std::endl;

  fmpz_add(tmp, checker_0->evalH, checker_1->evalH);
  fmpz_mod(tmp, tmp, Int_Modulus);
  std::cout << "r * h(r) = "; fmpz_print(tmp); std::cout << std::endl;

  fmpz_clear(rgr);
  fmpz_clear(tmp);

  fmpz_clear(out0);
  fmpz_clear(out1);
  delete cor0;
  delete cor1;
  delete corshare0;
  delete corshare1;
  delete pre1;
  delete checker_1;
  delete var_circuit1;
  delete pre0;
  delete checker_0;
  delete var_circuit0;
  fmpz_clear(randomX);

  delete p0;
  delete p1;
  fmpz_clear(inp[0]);
  fmpz_clear(inp[1]);
}

int main(int argc, char* argv[])
{
  init_constants();

  test_CheckVar();

  clear_constants();
  return 0;
}
