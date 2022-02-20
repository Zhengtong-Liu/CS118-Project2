// thread example
#include <iostream>       // std::cout
#include <thread>         // std::thread
 
int i = 0;
void foo() 
{
   i++;
   std::cout << "foo" << i << std::endl;
}

void bar(int x)
{
  i = i+x;
  std::cout << "bar" << i << std::endl;
}

int main() 
{
  std::thread first (foo);     // spawn new thread that calls foo()
  std::thread second (bar,1);  // spawn new thread that calls bar(0)

  std::cout << "main, foo and bar now execute concurrently...\n";

  // synchronize threads:
  first.join();                // pauses until first finishes
  second.join();               // pauses until second finishes

  std::cout << "foo and bar completed.\n";

  return 0;
}