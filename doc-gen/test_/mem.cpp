#include <iostream>
using namespace std;

static int getMemoryBlockIndex(size_t length) {
    return (length >> 4) + bool(length & 15);
}

int main(int argc, char const *argv[])
{
  // for (int i = 0; i < 100; ++i)
  // {
  //   cout << i << ": " << getMemoryBlockIndex(i) << endl;
  // }
    cout << 1 << ": " << getMemoryBlockIndex(1) << endl;
  return 0;
}