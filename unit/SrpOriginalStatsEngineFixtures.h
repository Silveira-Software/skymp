// Generated from captured AE 1.6.1170 instructions in Unicorn; not a live Skyrim test.
// Distribution SHA256 90ffdf94b996d3c038cc4d11965dbbf5399663142b87968fb85255c79422a779
// GetLevel SHA256 1a33c10d0ffb8055f24dc1430560bc28043e2b929a67de14eef12366fa3b6b77
#pragma once
#include <array>
namespace OriginalEngineFixtures {
struct Allocation { uint16_t level; std::array<uint8_t,3> weights; uint32_t budget; std::array<int,3> growth; };
constexpr Allocation allocations[] = {
  {1, {1,1,1}, 7, {0,0,0}},
  {1, {1,1,1}, 10, {0,0,0}},
  {2, {1,1,1}, 7, {2,2,3}},
  {2, {1,1,1}, 10, {3,3,4}},
  {3, {1,1,1}, 7, {4,5,5}},
  {3, {1,1,1}, 10, {6,7,7}},
  {7, {1,1,1}, 7, {14,14,14}},
  {7, {1,1,1}, 10, {20,20,20}},
  {12, {1,1,1}, 7, {25,26,26}},
  {12, {1,1,1}, 10, {36,37,37}},
  {28, {1,1,1}, 7, {63,63,63}},
  {28, {1,1,1}, 10, {90,90,90}},
  {80, {1,1,1}, 7, {184,184,185}},
  {80, {1,1,1}, 10, {263,263,264}},
  {100, {1,1,1}, 7, {231,231,231}},
  {100, {1,1,1}, 10, {330,330,330}},
  {1, {3,0,3}, 7, {0,0,0}},
  {1, {3,0,3}, 10, {0,0,0}},
  {2, {3,0,3}, 7, {3,0,4}},
  {2, {3,0,3}, 10, {5,0,5}},
  {3, {3,0,3}, 7, {7,0,7}},
  {3, {3,0,3}, 10, {10,0,10}},
  {7, {3,0,3}, 7, {21,0,21}},
  {7, {3,0,3}, 10, {30,0,30}},
  {12, {3,0,3}, 7, {38,0,39}},
  {12, {3,0,3}, 10, {55,0,55}},
  {28, {3,0,3}, 7, {94,0,95}},
  {28, {3,0,3}, 10, {135,0,135}},
  {80, {3,0,3}, 7, {276,0,277}},
  {80, {3,0,3}, 10, {395,0,395}},
  {100, {3,0,3}, 7, {346,0,347}},
  {100, {3,0,3}, 10, {495,0,495}},
  {1, {2,3,0}, 7, {0,0,0}},
  {1, {2,3,0}, 10, {0,0,0}},
  {2, {2,3,0}, 7, {3,4,0}},
  {2, {2,3,0}, 10, {4,6,0}},
  {3, {2,3,0}, 7, {6,8,0}},
  {3, {2,3,0}, 10, {8,12,0}},
  {7, {2,3,0}, 7, {17,25,0}},
  {7, {2,3,0}, 10, {24,36,0}},
  {12, {2,3,0}, 7, {31,46,0}},
  {12, {2,3,0}, 10, {44,66,0}},
  {28, {2,3,0}, 7, {76,113,0}},
  {28, {2,3,0}, 10, {108,162,0}},
  {80, {2,3,0}, 7, {221,332,0}},
  {80, {2,3,0}, 10, {316,474,0}},
  {100, {2,3,0}, 7, {277,416,0}},
  {100, {2,3,0}, 10, {396,594,0}},
  {1, {3,0,2}, 7, {0,0,0}},
  {1, {3,0,2}, 10, {0,0,0}},
  {2, {3,0,2}, 7, {4,0,3}},
  {2, {3,0,2}, 10, {6,0,4}},
  {3, {3,0,2}, 7, {8,0,6}},
  {3, {3,0,2}, 10, {12,0,8}},
  {7, {3,0,2}, 7, {25,0,17}},
  {7, {3,0,2}, 10, {36,0,24}},
  {12, {3,0,2}, 7, {46,0,31}},
  {12, {3,0,2}, 10, {66,0,44}},
  {28, {3,0,2}, 7, {113,0,76}},
  {28, {3,0,2}, 10, {162,0,108}},
  {80, {3,0,2}, 7, {332,0,221}},
  {80, {3,0,2}, 10, {474,0,316}},
  {100, {3,0,2}, 7, {416,0,277}},
  {100, {3,0,2}, 10, {594,0,396}},
  {1, {1,4,0}, 7, {0,0,0}},
  {1, {1,4,0}, 10, {0,0,0}},
  {2, {1,4,0}, 7, {2,5,0}},
  {2, {1,4,0}, 10, {2,8,0}},
  {3, {1,4,0}, 7, {3,11,0}},
  {3, {1,4,0}, 10, {4,16,0}},
  {7, {1,4,0}, 7, {9,33,0}},
  {7, {1,4,0}, 10, {12,48,0}},
  {12, {1,4,0}, 7, {16,61,0}},
  {12, {1,4,0}, 10, {22,88,0}},
  {28, {1,4,0}, 7, {38,151,0}},
  {28, {1,4,0}, 10, {54,216,0}},
  {80, {1,4,0}, 7, {111,442,0}},
  {80, {1,4,0}, 10, {158,632,0}},
  {100, {1,4,0}, 7, {139,554,0}},
  {100, {1,4,0}, 10, {198,792,0}},
  {1, {2,1,2}, 7, {0,0,0}},
  {1, {2,1,2}, 10, {0,0,0}},
  {2, {2,1,2}, 7, {3,1,3}},
  {2, {2,1,2}, 10, {4,2,4}},
  {3, {2,1,2}, 7, {5,3,6}},
  {3, {2,1,2}, 10, {8,4,8}},
  {7, {2,1,2}, 7, {17,8,17}},
  {7, {2,1,2}, 10, {24,12,24}},
  {12, {2,1,2}, 7, {31,15,31}},
  {12, {2,1,2}, 10, {44,22,44}},
  {28, {2,1,2}, 7, {75,38,76}},
  {28, {2,1,2}, 10, {108,54,108}},
  {80, {2,1,2}, 7, {221,111,221}},
  {80, {2,1,2}, 10, {316,158,316}},
  {100, {2,1,2}, 7, {277,139,277}},
  {100, {2,1,2}, 10, {396,198,396}},
  {1, {0,0,0}, 7, {0,0,0}},
  {1, {0,0,0}, 10, {0,0,0}},
  {2, {0,0,0}, 7, {0,0,0}},
  {2, {0,0,0}, 10, {0,0,0}},
  {3, {0,0,0}, 7, {0,0,0}},
  {3, {0,0,0}, 10, {0,0,0}},
  {7, {0,0,0}, 7, {0,0,0}},
  {7, {0,0,0}, 10, {0,0,0}},
  {12, {0,0,0}, 7, {0,0,0}},
  {12, {0,0,0}, 10, {0,0,0}},
  {28, {0,0,0}, 7, {0,0,0}},
  {28, {0,0,0}, 10, {0,0,0}},
  {80, {0,0,0}, 7, {0,0,0}},
  {80, {0,0,0}, 10, {0,0,0}},
  {100, {0,0,0}, 7, {0,0,0}},
  {100, {0,0,0}, 10, {0,0,0}},
  {1, {0,1,0}, 7, {0,0,0}},
  {1, {0,1,0}, 10, {0,0,0}},
  {2, {0,1,0}, 7, {0,7,0}},
  {2, {0,1,0}, 10, {0,10,0}},
  {3, {0,1,0}, 7, {0,14,0}},
  {3, {0,1,0}, 10, {0,20,0}},
  {7, {0,1,0}, 7, {0,42,0}},
  {7, {0,1,0}, 10, {0,60,0}},
  {12, {0,1,0}, 7, {0,77,0}},
  {12, {0,1,0}, 10, {0,110,0}},
  {28, {0,1,0}, 7, {0,189,0}},
  {28, {0,1,0}, 10, {0,270,0}},
  {80, {0,1,0}, 7, {0,553,0}},
  {80, {0,1,0}, 10, {0,790,0}},
  {100, {0,1,0}, 7, {0,693,0}},
  {100, {0,1,0}, 10, {0,990,0}},
  {1, {0,0,1}, 7, {0,0,0}},
  {1, {0,0,1}, 10, {0,0,0}},
  {2, {0,0,1}, 7, {0,0,7}},
  {2, {0,0,1}, 10, {0,0,10}},
  {3, {0,0,1}, 7, {0,0,14}},
  {3, {0,0,1}, 10, {0,0,20}},
  {7, {0,0,1}, 7, {0,0,42}},
  {7, {0,0,1}, 10, {0,0,60}},
  {12, {0,0,1}, 7, {0,0,77}},
  {12, {0,0,1}, 10, {0,0,110}},
  {28, {0,0,1}, 7, {0,0,189}},
  {28, {0,0,1}, 10, {0,0,270}},
  {80, {0,0,1}, 7, {0,0,553}},
  {80, {0,0,1}, 10, {0,0,790}},
  {100, {0,0,1}, 7, {0,0,693}},
  {100, {0,0,1}, 10, {0,0,990}},
  {1, {255,254,253}, 7, {0,0,0}},
  {1, {255,254,253}, 10, {0,0,0}},
  {2, {255,254,253}, 7, {3,2,2}},
  {2, {255,254,253}, 10, {4,3,3}},
  {3, {255,254,253}, 7, {5,5,4}},
  {3, {255,254,253}, 10, {7,7,6}},
  {7, {255,254,253}, 7, {14,14,14}},
  {7, {255,254,253}, 10, {20,20,20}},
  {12, {255,254,253}, 7, {26,26,25}},
  {12, {255,254,253}, 10, {37,37,36}},
  {28, {255,254,253}, 7, {63,63,63}},
  {28, {255,254,253}, 10, {90,90,90}},
  {80, {255,254,253}, 7, {185,184,184}},
  {80, {255,254,253}, 10, {265,263,262}},
  {100, {255,254,253}, 7, {231,231,231}},
  {100, {255,254,253}, 10, {331,330,329}},
};
struct Level { uint16_t value, minimum, maximum, pcLevel, expected; bool pc; };
constexpr Level levels[] = {
  {1000,15,45,1,15,true}, // ca4:mihailhighfantasypack.esp
  {1000,15,45,3,15,true}, // ca4:mihailhighfantasypack.esp
  {1000,15,45,10,15,true}, // ca4:mihailhighfantasypack.esp
  {1000,15,45,80,45,true}, // ca4:mihailhighfantasypack.esp
  {1000,10,35,1,10,true}, // cac:mihailhighfantasypack.esp
  {1000,10,35,3,10,true}, // cac:mihailhighfantasypack.esp
  {1000,10,35,10,10,true}, // cac:mihailhighfantasypack.esp
  {1000,10,35,80,35,true}, // cac:mihailhighfantasypack.esp
  {1000,12,22,1,12,true}, // 883:mihailhighfantasypack.esp
  {1000,12,22,3,12,true}, // 883:mihailhighfantasypack.esp
  {1000,12,22,10,12,true}, // 883:mihailhighfantasypack.esp
  {1000,12,22,80,22,true}, // 883:mihailhighfantasypack.esp
  {500,1,0,1,1,true}, // cf89d:Skyrim.esm
  {500,1,0,3,1,true}, // cf89d:Skyrim.esm
  {500,1,0,10,5,true}, // cf89d:Skyrim.esm
  {500,1,0,80,40,true}, // cf89d:Skyrim.esm
  {1000,4,10,1,4,true}, // 1ee63:mihailhighfantasypack.esp
  {1000,4,10,3,4,true}, // 1ee63:mihailhighfantasypack.esp
  {1000,4,10,10,10,true}, // 1ee63:mihailhighfantasypack.esp
  {1000,4,10,80,10,true}, // 1ee63:mihailhighfantasypack.esp
  {1000,2,10,1,2,true}, // 87d:mihailhighfantasypack.esp
  {1000,2,10,3,3,true}, // 87d:mihailhighfantasypack.esp
  {1000,2,10,10,10,true}, // 87d:mihailhighfantasypack.esp
  {1000,2,10,80,10,true}, // 87d:mihailhighfantasypack.esp
  {1000,2,10,1,2,true}, // 87f:mihailhighfantasypack.esp
  {1000,2,10,3,3,true}, // 87f:mihailhighfantasypack.esp
  {1000,2,10,10,10,true}, // 87f:mihailhighfantasypack.esp
  {1000,2,10,80,10,true}, // 87f:mihailhighfantasypack.esp
  {1000,6,15,1,6,true}, // 882:mihailhighfantasypack.esp
  {1000,6,15,3,6,true}, // 882:mihailhighfantasypack.esp
  {1000,6,15,10,10,true}, // 882:mihailhighfantasypack.esp
  {1000,6,15,80,15,true}, // 882:mihailhighfantasypack.esp
  {1000,1,0,1,1,true}, // 23aba:Skyrim.esm
  {1000,1,0,3,3,true}, // 23aba:Skyrim.esm
  {1000,1,0,10,10,true}, // 23aba:Skyrim.esm
  {1000,1,0,80,80,true}, // 23aba:Skyrim.esm
  {1000,9,20,1,9,true}, // 590d:mihailhighfantasypack.esp
  {1000,9,20,3,9,true}, // 590d:mihailhighfantasypack.esp
  {1000,9,20,10,10,true}, // 590d:mihailhighfantasypack.esp
  {1000,9,20,80,20,true}, // 590d:mihailhighfantasypack.esp
  {1000,15,50,1,15,true}, // 4c755:mihailhighfantasypack.esp
  {1000,15,50,3,15,true}, // 4c755:mihailhighfantasypack.esp
  {1000,15,50,10,15,true}, // 4c755:mihailhighfantasypack.esp
  {1000,15,50,80,50,true}, // 4c755:mihailhighfantasypack.esp
  {1000,17,35,1,17,true}, // d59:mihailhighfantasypack.esp
  {1000,17,35,3,17,true}, // d59:mihailhighfantasypack.esp
  {1000,17,35,10,17,true}, // d59:mihailhighfantasypack.esp
  {1000,17,35,80,35,true}, // d59:mihailhighfantasypack.esp
  {1000,20,40,1,20,true}, // d66:mihailhighfantasypack.esp
  {1000,20,40,3,20,true}, // d66:mihailhighfantasypack.esp
  {1000,20,40,10,20,true}, // d66:mihailhighfantasypack.esp
  {1000,20,40,80,40,true}, // d66:mihailhighfantasypack.esp
  {1000,15,30,1,15,true}, // d56:mihailhighfantasypack.esp
  {1000,15,30,3,15,true}, // d56:mihailhighfantasypack.esp
  {1000,15,30,10,15,true}, // d56:mihailhighfantasypack.esp
  {1000,15,30,80,30,true}, // d56:mihailhighfantasypack.esp
  {1000,10,15,1,10,true}, // c51:mihailhighfantasypack.esp
  {1000,10,15,3,10,true}, // c51:mihailhighfantasypack.esp
  {1000,10,15,10,10,true}, // c51:mihailhighfantasypack.esp
  {1000,10,15,80,15,true}, // c51:mihailhighfantasypack.esp
  {1000,4,9,1,4,true}, // c4d:mihailhighfantasypack.esp
  {1000,4,9,3,4,true}, // c4d:mihailhighfantasypack.esp
  {1000,4,9,10,9,true}, // c4d:mihailhighfantasypack.esp
  {1000,4,9,80,9,true}, // c4d:mihailhighfantasypack.esp
  {1000,15,40,1,15,true}, // 9a7:mihailhighfantasypack.esp
  {1000,15,40,3,15,true}, // 9a7:mihailhighfantasypack.esp
  {1000,15,40,10,15,true}, // 9a7:mihailhighfantasypack.esp
  {1000,15,40,80,40,true}, // 9a7:mihailhighfantasypack.esp
  {1000,15,50,1,15,true}, // b8f:mihailhighfantasypack.esp
  {1000,15,50,3,15,true}, // b8f:mihailhighfantasypack.esp
  {1000,15,50,10,15,true}, // b8f:mihailhighfantasypack.esp
  {1000,15,50,80,50,true}, // b8f:mihailhighfantasypack.esp
  {1000,15,50,1,15,true}, // 4c75c:mihailhighfantasypack.esp
  {1000,15,50,3,15,true}, // 4c75c:mihailhighfantasypack.esp
  {1000,15,50,10,15,true}, // 4c75c:mihailhighfantasypack.esp
  {1000,15,50,80,50,true}, // 4c75c:mihailhighfantasypack.esp
  {1,0,100,1,1,false}, // 19e1c:mihailhighfantasypack.esp
  {1,0,100,3,1,false}, // 19e1c:mihailhighfantasypack.esp
  {1,0,100,10,1,false}, // 19e1c:mihailhighfantasypack.esp
  {1,0,100,80,1,false}, // 19e1c:mihailhighfantasypack.esp
  {500,1,0,1,1,true}, // 23ab8:Skyrim.esm
  {500,1,0,3,1,true}, // 23ab8:Skyrim.esm
  {500,1,0,10,5,true}, // 23ab8:Skyrim.esm
  {500,1,0,80,40,true}, // 23ab8:Skyrim.esm
  {1000,6,20,1,6,true}, // d03:mihailhighfantasypack.esp
  {1000,6,20,3,6,true}, // d03:mihailhighfantasypack.esp
  {1000,6,20,10,10,true}, // d03:mihailhighfantasypack.esp
  {1000,6,20,80,20,true}, // d03:mihailhighfantasypack.esp
  {1000,10,35,1,10,true}, // cf4:mihailhighfantasypack.esp
  {1000,10,35,3,10,true}, // cf4:mihailhighfantasypack.esp
  {1000,10,35,10,10,true}, // cf4:mihailhighfantasypack.esp
  {1000,10,35,80,35,true}, // cf4:mihailhighfantasypack.esp
  {1000,22,45,1,22,true}, // be4:mihailhighfantasypack.esp
  {1000,22,45,3,22,true}, // be4:mihailhighfantasypack.esp
  {1000,22,45,10,22,true}, // be4:mihailhighfantasypack.esp
  {1000,22,45,80,45,true}, // be4:mihailhighfantasypack.esp
  {1000,1,0,1,1,true}, // 23abb:Skyrim.esm
  {1000,1,0,3,3,true}, // 23abb:Skyrim.esm
  {1000,1,0,10,10,true}, // 23abb:Skyrim.esm
  {1000,1,0,80,80,true}, // 23abb:Skyrim.esm
  {1000,15,50,1,15,true}, // 96f:mihailhighfantasypack.esp
  {1000,15,50,3,15,true}, // 96f:mihailhighfantasypack.esp
  {1000,15,50,10,15,true}, // 96f:mihailhighfantasypack.esp
  {1000,15,50,80,50,true}, // 96f:mihailhighfantasypack.esp
  {1000,18,55,1,18,true}, // 47665:mihailhighfantasypack.esp
  {1000,18,55,3,18,true}, // 47665:mihailhighfantasypack.esp
  {1000,18,55,10,18,true}, // 47665:mihailhighfantasypack.esp
  {1000,18,55,80,55,true}, // 47665:mihailhighfantasypack.esp
  {1000,15,40,1,15,true}, // a72:mihailhighfantasypack.esp
  {1000,15,40,3,15,true}, // a72:mihailhighfantasypack.esp
  {1000,15,40,10,15,true}, // a72:mihailhighfantasypack.esp
  {1000,15,40,80,40,true}, // a72:mihailhighfantasypack.esp
  {1000,18,30,1,18,true}, // c53:mihailhighfantasypack.esp
  {1000,18,30,3,18,true}, // c53:mihailhighfantasypack.esp
  {1000,18,30,10,18,true}, // c53:mihailhighfantasypack.esp
  {1000,18,30,80,30,true}, // c53:mihailhighfantasypack.esp
  {1000,50,70,1,50,true}, // 9d879:mihailhighfantasypack.esp
  {1000,50,70,3,50,true}, // 9d879:mihailhighfantasypack.esp
  {1000,50,70,10,50,true}, // 9d879:mihailhighfantasypack.esp
  {1000,50,70,80,70,true}, // 9d879:mihailhighfantasypack.esp
  {1000,15,50,1,15,true}, // 4c76c:mihailhighfantasypack.esp
  {1000,15,50,3,15,true}, // 4c76c:mihailhighfantasypack.esp
  {1000,15,50,10,15,true}, // 4c76c:mihailhighfantasypack.esp
  {1000,15,50,80,50,true}, // 4c76c:mihailhighfantasypack.esp
  {1000,16,30,1,16,true}, // c48:mihailhighfantasypack.esp
  {1000,16,30,3,16,true}, // c48:mihailhighfantasypack.esp
  {1000,16,30,10,16,true}, // c48:mihailhighfantasypack.esp
  {1000,16,30,80,30,true}, // c48:mihailhighfantasypack.esp
  {1000,4,10,1,4,true}, // 871:mihailhighfantasypack.esp
  {1000,4,10,3,4,true}, // 871:mihailhighfantasypack.esp
  {1000,4,10,10,10,true}, // 871:mihailhighfantasypack.esp
  {1000,4,10,80,10,true}, // 871:mihailhighfantasypack.esp
  {1000,4,10,1,4,true}, // 873:mihailhighfantasypack.esp
  {1000,4,10,3,4,true}, // 873:mihailhighfantasypack.esp
  {1000,4,10,10,10,true}, // 873:mihailhighfantasypack.esp
  {1000,4,10,80,10,true}, // 873:mihailhighfantasypack.esp
  {1000,5,12,1,5,true}, // 1ee64:mihailhighfantasypack.esp
  {1000,5,12,3,5,true}, // 1ee64:mihailhighfantasypack.esp
  {1000,5,12,10,10,true}, // 1ee64:mihailhighfantasypack.esp
  {1000,5,12,80,12,true}, // 1ee64:mihailhighfantasypack.esp
  {1000,6,12,1,6,true}, // 874:mihailhighfantasypack.esp
  {1000,6,12,3,6,true}, // 874:mihailhighfantasypack.esp
  {1000,6,12,10,10,true}, // 874:mihailhighfantasypack.esp
  {1000,6,12,80,12,true}, // 874:mihailhighfantasypack.esp
  {1000,8,15,1,8,true}, // 875:mihailhighfantasypack.esp
  {1000,8,15,3,8,true}, // 875:mihailhighfantasypack.esp
  {1000,8,15,10,10,true}, // 875:mihailhighfantasypack.esp
  {1000,8,15,80,15,true}, // 875:mihailhighfantasypack.esp
  {50,10,0,1,50,false}, // e25a:Dawnguard.esm
  {50,10,0,3,50,false}, // e25a:Dawnguard.esm
  {50,10,0,10,50,false}, // e25a:Dawnguard.esm
  {50,10,0,80,50,false}, // e25a:Dawnguard.esm
  {1,0,0,1,1,false}, // f797:Dawnguard.esm
  {1,0,0,3,1,false}, // f797:Dawnguard.esm
  {1,0,0,10,1,false}, // f797:Dawnguard.esm
  {1,0,0,80,1,false}, // f797:Dawnguard.esm
  {50,10,0,1,50,false}, // 1ee11:mihailhighfantasypack.esp
  {50,10,0,3,50,false}, // 1ee11:mihailhighfantasypack.esp
  {50,10,0,10,50,false}, // 1ee11:mihailhighfantasypack.esp
  {50,10,0,80,50,false}, // 1ee11:mihailhighfantasypack.esp
  {1000,3,6,1,3,true}, // 3839b:mihailhighfantasypack.esp
  {1000,3,6,3,3,true}, // 3839b:mihailhighfantasypack.esp
  {1000,3,6,10,6,true}, // 3839b:mihailhighfantasypack.esp
  {1000,3,6,80,6,true}, // 3839b:mihailhighfantasypack.esp
  {1000,4,7,1,4,true}, // 4c7ca:mihailhighfantasypack.esp
  {1000,4,7,3,4,true}, // 4c7ca:mihailhighfantasypack.esp
  {1000,4,7,10,7,true}, // 4c7ca:mihailhighfantasypack.esp
  {1000,4,7,80,7,true}, // 4c7ca:mihailhighfantasypack.esp
  {1000,45,80,1,45,true}, // 425a5:mihailhighfantasypack.esp
  {1000,45,80,3,45,true}, // 425a5:mihailhighfantasypack.esp
  {1000,45,80,10,45,true}, // 425a5:mihailhighfantasypack.esp
  {1000,45,80,80,80,true}, // 425a5:mihailhighfantasypack.esp
  {1000,60,90,1,60,true}, // 839:mihailhighfantasypack.esp
  {1000,60,90,3,60,true}, // 839:mihailhighfantasypack.esp
  {1000,60,90,10,60,true}, // 839:mihailhighfantasypack.esp
  {1000,60,90,80,80,true}, // 839:mihailhighfantasypack.esp
  {1000,4,9,1,4,true}, // c50:mihailhighfantasypack.esp
  {1000,4,9,3,4,true}, // c50:mihailhighfantasypack.esp
  {1000,4,9,10,9,true}, // c50:mihailhighfantasypack.esp
  {1000,4,9,80,9,true}, // c50:mihailhighfantasypack.esp
  {1,6,0,1,1,false}, // 39d31:Skyrim.esm
  {1,6,0,3,1,false}, // 39d31:Skyrim.esm
  {1,6,0,10,1,false}, // 39d31:Skyrim.esm
  {1,6,0,80,1,false}, // 39d31:Skyrim.esm
  {1000,20,30,1,20,true}, // 83f:mihailhighfantasypack.esp
  {1000,20,30,3,20,true}, // 83f:mihailhighfantasypack.esp
  {1000,20,30,10,20,true}, // 83f:mihailhighfantasypack.esp
  {1000,20,30,80,30,true}, // 83f:mihailhighfantasypack.esp
  {1000,22,45,1,22,true}, // 591b:mihailhighfantasypack.esp
  {1000,22,45,3,22,true}, // 591b:mihailhighfantasypack.esp
  {1000,22,45,10,22,true}, // 591b:mihailhighfantasypack.esp
  {1000,22,45,80,45,true}, // 591b:mihailhighfantasypack.esp
  {1000,7,10,1,7,true}, // 591c:mihailhighfantasypack.esp
  {1000,7,10,3,7,true}, // 591c:mihailhighfantasypack.esp
  {1000,7,10,10,10,true}, // 591c:mihailhighfantasypack.esp
  {1000,7,10,80,10,true}, // 591c:mihailhighfantasypack.esp
  {500,1,0,1,1,true}, // 23ab1:Skyrim.esm
  {500,1,0,3,1,true}, // 23ab1:Skyrim.esm
  {500,1,0,10,5,true}, // 23ab1:Skyrim.esm
  {500,1,0,80,40,true}, // 23ab1:Skyrim.esm
  {1000,24,34,1,24,true}, // 80b:mihailhighfantasypack.esp
  {1000,24,34,3,24,true}, // 80b:mihailhighfantasypack.esp
  {1000,24,34,10,24,true}, // 80b:mihailhighfantasypack.esp
  {1000,24,34,80,34,true}, // 80b:mihailhighfantasypack.esp
  {1000,26,36,1,26,true}, // 80c:mihailhighfantasypack.esp
  {1000,26,36,3,26,true}, // 80c:mihailhighfantasypack.esp
  {1000,26,36,10,26,true}, // 80c:mihailhighfantasypack.esp
  {1000,26,36,80,36,true}, // 80c:mihailhighfantasypack.esp
  {1000,42,50,1,42,true}, // 808:mihailhighfantasypack.esp
  {1000,42,50,3,42,true}, // 808:mihailhighfantasypack.esp
  {1000,42,50,10,42,true}, // 808:mihailhighfantasypack.esp
  {1000,42,50,80,50,true}, // 808:mihailhighfantasypack.esp
  {1000,25,37,1,25,true}, // 850:mihailhighfantasypack.esp
  {1000,25,37,3,25,true}, // 850:mihailhighfantasypack.esp
  {1000,25,37,10,25,true}, // 850:mihailhighfantasypack.esp
  {1000,25,37,80,37,true}, // 850:mihailhighfantasypack.esp
  {1000,40,47,1,40,true}, // fb17:mihailhighfantasypack.esp
  {1000,40,47,3,40,true}, // fb17:mihailhighfantasypack.esp
  {1000,40,47,10,40,true}, // fb17:mihailhighfantasypack.esp
  {1000,40,47,80,47,true}, // fb17:mihailhighfantasypack.esp
  {1000,22,50,1,22,true}, // 2eee5:mihailhighfantasypack.esp
  {1000,22,50,3,22,true}, // 2eee5:mihailhighfantasypack.esp
  {1000,22,50,10,22,true}, // 2eee5:mihailhighfantasypack.esp
  {1000,22,50,80,50,true}, // 2eee5:mihailhighfantasypack.esp
  {1000,6,12,1,6,true}, // 893:mihailhighfantasypack.esp
  {1000,6,12,3,6,true}, // 893:mihailhighfantasypack.esp
  {1000,6,12,10,10,true}, // 893:mihailhighfantasypack.esp
  {1000,6,12,80,12,true}, // 893:mihailhighfantasypack.esp
  {1000,4,10,1,4,true}, // 891:mihailhighfantasypack.esp
  {1000,4,10,3,4,true}, // 891:mihailhighfantasypack.esp
  {1000,4,10,10,10,true}, // 891:mihailhighfantasypack.esp
  {1000,4,10,80,10,true}, // 891:mihailhighfantasypack.esp
  {1000,20,42,1,20,true}, // 19d11:mihailhighfantasypack.esp
  {1000,20,42,3,20,true}, // 19d11:mihailhighfantasypack.esp
  {1000,20,42,10,20,true}, // 19d11:mihailhighfantasypack.esp
  {1000,20,42,80,42,true}, // 19d11:mihailhighfantasypack.esp
  {1000,18,30,1,18,true}, // c54:mihailhighfantasypack.esp
  {1000,18,30,3,18,true}, // c54:mihailhighfantasypack.esp
  {1000,18,30,10,18,true}, // c54:mihailhighfantasypack.esp
  {1000,18,30,80,30,true}, // c54:mihailhighfantasypack.esp
  {1000,10,15,1,10,true}, // c52:mihailhighfantasypack.esp
  {1000,10,15,3,10,true}, // c52:mihailhighfantasypack.esp
  {1000,10,15,10,10,true}, // c52:mihailhighfantasypack.esp
  {1000,10,15,80,15,true}, // c52:mihailhighfantasypack.esp
  {1000,4,10,1,4,true}, // fb5a:mihailhighfantasypack.esp
  {1000,4,10,3,4,true}, // fb5a:mihailhighfantasypack.esp
  {1000,4,10,10,10,true}, // fb5a:mihailhighfantasypack.esp
  {1000,4,10,80,10,true}, // fb5a:mihailhighfantasypack.esp
  {1000,15,50,1,15,true}, // 4c76a:mihailhighfantasypack.esp
  {1000,15,50,3,15,true}, // 4c76a:mihailhighfantasypack.esp
  {1000,15,50,10,15,true}, // 4c76a:mihailhighfantasypack.esp
  {1000,15,50,80,50,true}, // 4c76a:mihailhighfantasypack.esp
  {30,0,0,1,30,false}, // a65:mihailhighfantasypack.esp
  {30,0,0,3,30,false}, // a65:mihailhighfantasypack.esp
  {30,0,0,10,30,false}, // a65:mihailhighfantasypack.esp
  {30,0,0,80,30,false}, // a65:mihailhighfantasypack.esp
  {9,0,0,1,9,false}, // 23ab3:Skyrim.esm
  {9,0,0,3,9,false}, // 23ab3:Skyrim.esm
  {9,0,0,10,9,false}, // 23ab3:Skyrim.esm
  {9,0,0,80,9,false}, // 23ab3:Skyrim.esm
  {1000,10,30,1,10,true}, // 8f0:mihailhighfantasypack.esp
  {1000,10,30,3,10,true}, // 8f0:mihailhighfantasypack.esp
  {1000,10,30,10,10,true}, // 8f0:mihailhighfantasypack.esp
  {1000,10,30,80,30,true}, // 8f0:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // d249a:Skyrim.esm
  {1,0,0,3,1,false}, // d249a:Skyrim.esm
  {1,0,0,10,1,false}, // d249a:Skyrim.esm
  {1,0,0,80,1,false}, // d249a:Skyrim.esm
  {2,0,0,1,2,false}, // 1ef1c:mihailhighfantasypack.esp
  {2,0,0,3,2,false}, // 1ef1c:mihailhighfantasypack.esp
  {2,0,0,10,2,false}, // 1ef1c:mihailhighfantasypack.esp
  {2,0,0,80,2,false}, // 1ef1c:mihailhighfantasypack.esp
  {1000,5,10,1,5,true}, // b50:mihailhighfantasypack.esp
  {1000,5,10,3,5,true}, // b50:mihailhighfantasypack.esp
  {1000,5,10,10,10,true}, // b50:mihailhighfantasypack.esp
  {1000,5,10,80,10,true}, // b50:mihailhighfantasypack.esp
  {1000,5,10,1,5,true}, // b30:mihailhighfantasypack.esp
  {1000,5,10,3,5,true}, // b30:mihailhighfantasypack.esp
  {1000,5,10,10,10,true}, // b30:mihailhighfantasypack.esp
  {1000,5,10,80,10,true}, // b30:mihailhighfantasypack.esp
  {12,10,0,1,12,false}, // a8f:mihailhighfantasypack.esp
  {12,10,0,3,12,false}, // a8f:mihailhighfantasypack.esp
  {12,10,0,10,12,false}, // a8f:mihailhighfantasypack.esp
  {12,10,0,80,12,false}, // a8f:mihailhighfantasypack.esp
  {1000,20,42,1,20,true}, // c15:mihailhighfantasypack.esp
  {1000,20,42,3,20,true}, // c15:mihailhighfantasypack.esp
  {1000,20,42,10,20,true}, // c15:mihailhighfantasypack.esp
  {1000,20,42,80,42,true}, // c15:mihailhighfantasypack.esp
  {1000,17,37,1,17,true}, // 14c0a:mihailhighfantasypack.esp
  {1000,17,37,3,17,true}, // 14c0a:mihailhighfantasypack.esp
  {1000,17,37,10,17,true}, // 14c0a:mihailhighfantasypack.esp
  {1000,17,37,80,37,true}, // 14c0a:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // a17:mihailhighfantasypack.esp
  {1,0,0,3,1,false}, // a17:mihailhighfantasypack.esp
  {1,0,0,10,1,false}, // a17:mihailhighfantasypack.esp
  {1,0,0,80,1,false}, // a17:mihailhighfantasypack.esp
  {28,10,0,1,28,false}, // aaa:mihailhighfantasypack.esp
  {28,10,0,3,28,false}, // aaa:mihailhighfantasypack.esp
  {28,10,0,10,28,false}, // aaa:mihailhighfantasypack.esp
  {28,10,0,80,28,false}, // aaa:mihailhighfantasypack.esp
  {1000,1,0,1,1,true}, // 23aae:Skyrim.esm
  {1000,1,0,3,3,true}, // 23aae:Skyrim.esm
  {1000,1,0,10,10,true}, // 23aae:Skyrim.esm
  {1000,1,0,80,80,true}, // 23aae:Skyrim.esm
  {28,10,0,1,28,false}, // a93:mihailhighfantasypack.esp
  {28,10,0,3,28,false}, // a93:mihailhighfantasypack.esp
  {28,10,0,10,28,false}, // a93:mihailhighfantasypack.esp
  {28,10,0,80,28,false}, // a93:mihailhighfantasypack.esp
  {1000,30,50,1,30,true}, // 65c7f:mihailhighfantasypack.esp
  {1000,30,50,3,30,true}, // 65c7f:mihailhighfantasypack.esp
  {1000,30,50,10,30,true}, // 65c7f:mihailhighfantasypack.esp
  {1000,30,50,80,50,true}, // 65c7f:mihailhighfantasypack.esp
  {1000,9,20,1,9,true}, // 876:mihailhighfantasypack.esp
  {1000,9,20,3,9,true}, // 876:mihailhighfantasypack.esp
  {1000,9,20,10,10,true}, // 876:mihailhighfantasypack.esp
  {1000,9,20,80,20,true}, // 876:mihailhighfantasypack.esp
  {1000,12,22,1,12,true}, // 877:mihailhighfantasypack.esp
  {1000,12,22,3,12,true}, // 877:mihailhighfantasypack.esp
  {1000,12,22,10,12,true}, // 877:mihailhighfantasypack.esp
  {1000,12,22,80,22,true}, // 877:mihailhighfantasypack.esp
  {1000,38,45,1,38,true}, // 15041:mihailhighfantasypack.esp
  {1000,38,45,3,38,true}, // 15041:mihailhighfantasypack.esp
  {1000,38,45,10,38,true}, // 15041:mihailhighfantasypack.esp
  {1000,38,45,80,45,true}, // 15041:mihailhighfantasypack.esp
  {1000,40,80,1,40,true}, // 7a101:mihailhighfantasypack.esp
  {1000,40,80,3,40,true}, // 7a101:mihailhighfantasypack.esp
  {1000,40,80,10,40,true}, // 7a101:mihailhighfantasypack.esp
  {1000,40,80,80,80,true}, // 7a101:mihailhighfantasypack.esp
  {1000,5,10,1,5,true}, // b39:mihailhighfantasypack.esp
  {1000,5,10,3,5,true}, // b39:mihailhighfantasypack.esp
  {1000,5,10,10,10,true}, // b39:mihailhighfantasypack.esp
  {1000,5,10,80,10,true}, // b39:mihailhighfantasypack.esp
  {1000,20,42,1,20,true}, // 19d17:mihailhighfantasypack.esp
  {1000,20,42,3,20,true}, // 19d17:mihailhighfantasypack.esp
  {1000,20,42,10,20,true}, // 19d17:mihailhighfantasypack.esp
  {1000,20,42,80,42,true}, // 19d17:mihailhighfantasypack.esp
  {1000,6,12,1,6,true}, // 19d1b:mihailhighfantasypack.esp
  {1000,6,12,3,6,true}, // 19d1b:mihailhighfantasypack.esp
  {1000,6,12,10,10,true}, // 19d1b:mihailhighfantasypack.esp
  {1000,6,12,80,12,true}, // 19d1b:mihailhighfantasypack.esp
  {1000,25,45,1,25,true}, // 19d14:mihailhighfantasypack.esp
  {1000,25,45,3,25,true}, // 19d14:mihailhighfantasypack.esp
  {1000,25,45,10,25,true}, // 19d14:mihailhighfantasypack.esp
  {1000,25,45,80,45,true}, // 19d14:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // 39cfd:Skyrim.esm
  {1,0,0,3,1,false}, // 39cfd:Skyrim.esm
  {1,0,0,10,1,false}, // 39cfd:Skyrim.esm
  {1,0,0,80,1,false}, // 39cfd:Skyrim.esm
  {1000,65,95,1,65,true}, // 590c:mihailhighfantasypack.esp
  {1000,65,95,3,65,true}, // 590c:mihailhighfantasypack.esp
  {1000,65,95,10,65,true}, // 590c:mihailhighfantasypack.esp
  {1000,65,95,80,80,true}, // 590c:mihailhighfantasypack.esp
  {1000,5,10,1,5,true}, // b49:mihailhighfantasypack.esp
  {1000,5,10,3,5,true}, // b49:mihailhighfantasypack.esp
  {1000,5,10,10,10,true}, // b49:mihailhighfantasypack.esp
  {1000,5,10,80,10,true}, // b49:mihailhighfantasypack.esp
  {1000,12,42,1,12,true}, // 9ce:mihailhighfantasypack.esp
  {1000,12,42,3,12,true}, // 9ce:mihailhighfantasypack.esp
  {1000,12,42,10,12,true}, // 9ce:mihailhighfantasypack.esp
  {1000,12,42,80,42,true}, // 9ce:mihailhighfantasypack.esp
  {1000,12,42,1,12,true}, // 9cf:mihailhighfantasypack.esp
  {1000,12,42,3,12,true}, // 9cf:mihailhighfantasypack.esp
  {1000,12,42,10,12,true}, // 9cf:mihailhighfantasypack.esp
  {1000,12,42,80,42,true}, // 9cf:mihailhighfantasypack.esp
  {4,0,0,1,4,false}, // 68d6d:Skyrim.esm
  {4,0,0,3,4,false}, // 68d6d:Skyrim.esm
  {4,0,0,10,4,false}, // 68d6d:Skyrim.esm
  {4,0,0,80,4,false}, // 68d6d:Skyrim.esm
  {1000,30,38,1,30,true}, // d27:mihailhighfantasypack.esp
  {1000,30,38,3,30,true}, // d27:mihailhighfantasypack.esp
  {1000,30,38,10,30,true}, // d27:mihailhighfantasypack.esp
  {1000,30,38,80,38,true}, // d27:mihailhighfantasypack.esp
  {1000,7,12,1,7,true}, // bac:mihailhighfantasypack.esp
  {1000,7,12,3,7,true}, // bac:mihailhighfantasypack.esp
  {1000,7,12,10,10,true}, // bac:mihailhighfantasypack.esp
  {1000,7,12,80,12,true}, // bac:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // aa05:mihailhighfantasypack.esp
  {1,0,0,3,1,false}, // aa05:mihailhighfantasypack.esp
  {1,0,0,10,1,false}, // aa05:mihailhighfantasypack.esp
  {1,0,0,80,1,false}, // aa05:mihailhighfantasypack.esp
  {1000,4,12,1,4,true}, // 590b:mihailhighfantasypack.esp
  {1000,4,12,3,4,true}, // 590b:mihailhighfantasypack.esp
  {1000,4,12,10,10,true}, // 590b:mihailhighfantasypack.esp
  {1000,4,12,80,12,true}, // 590b:mihailhighfantasypack.esp
  {1000,10,35,1,10,true}, // 98f:mihailhighfantasypack.esp
  {1000,10,35,3,10,true}, // 98f:mihailhighfantasypack.esp
  {1000,10,35,10,10,true}, // 98f:mihailhighfantasypack.esp
  {1000,10,35,80,35,true}, // 98f:mihailhighfantasypack.esp
  {1000,8,15,1,8,true}, // 4c79b:mihailhighfantasypack.esp
  {1000,8,15,3,8,true}, // 4c79b:mihailhighfantasypack.esp
  {1000,8,15,10,10,true}, // 4c79b:mihailhighfantasypack.esp
  {1000,8,15,80,15,true}, // 4c79b:mihailhighfantasypack.esp
  {1000,25,32,1,25,true}, // d21:mihailhighfantasypack.esp
  {1000,25,32,3,25,true}, // d21:mihailhighfantasypack.esp
  {1000,25,32,10,25,true}, // d21:mihailhighfantasypack.esp
  {1000,25,32,80,32,true}, // d21:mihailhighfantasypack.esp
  {1000,10,20,1,10,true}, // bc4:mihailhighfantasypack.esp
  {1000,10,20,3,10,true}, // bc4:mihailhighfantasypack.esp
  {1000,10,20,10,10,true}, // bc4:mihailhighfantasypack.esp
  {1000,10,20,80,20,true}, // bc4:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // 14c14:mihailhighfantasypack.esp
  {1,0,0,3,1,false}, // 14c14:mihailhighfantasypack.esp
  {1,0,0,10,1,false}, // 14c14:mihailhighfantasypack.esp
  {1,0,0,80,1,false}, // 14c14:mihailhighfantasypack.esp
  {1,10,0,1,1,false}, // ac3:mihailhighfantasypack.esp
  {1,10,0,3,1,false}, // ac3:mihailhighfantasypack.esp
  {1,10,0,10,1,false}, // ac3:mihailhighfantasypack.esp
  {1,10,0,80,1,false}, // ac3:mihailhighfantasypack.esp
  {1,10,0,1,1,false}, // ac5:mihailhighfantasypack.esp
  {1,10,0,3,1,false}, // ac5:mihailhighfantasypack.esp
  {1,10,0,10,1,false}, // ac5:mihailhighfantasypack.esp
  {1,10,0,80,1,false}, // ac5:mihailhighfantasypack.esp
  {1,10,0,1,1,false}, // aca:mihailhighfantasypack.esp
  {1,10,0,3,1,false}, // aca:mihailhighfantasypack.esp
  {1,10,0,10,1,false}, // aca:mihailhighfantasypack.esp
  {1,10,0,80,1,false}, // aca:mihailhighfantasypack.esp
  {1,10,0,1,1,false}, // abd:mihailhighfantasypack.esp
  {1,10,0,3,1,false}, // abd:mihailhighfantasypack.esp
  {1,10,0,10,1,false}, // abd:mihailhighfantasypack.esp
  {1,10,0,80,1,false}, // abd:mihailhighfantasypack.esp
  {5,0,0,1,5,false}, // 39d38:Skyrim.esm
  {5,0,0,3,5,false}, // 39d38:Skyrim.esm
  {5,0,0,10,5,false}, // 39d38:Skyrim.esm
  {5,0,0,80,5,false}, // 39d38:Skyrim.esm
  {6,0,0,1,6,false}, // 3def0:Skyrim.esm
  {6,0,0,3,6,false}, // 3def0:Skyrim.esm
  {6,0,0,10,6,false}, // 3def0:Skyrim.esm
  {6,0,0,80,6,false}, // 3def0:Skyrim.esm
  {1,0,100,1,1,false}, // 2d1fc:Skyrim.esm
  {1,0,100,3,1,false}, // 2d1fc:Skyrim.esm
  {1,0,100,10,1,false}, // 2d1fc:Skyrim.esm
  {1,0,100,80,1,false}, // 2d1fc:Skyrim.esm
  {1000,12,30,1,12,true}, // 913:mihailhighfantasypack.esp
  {1000,12,30,3,12,true}, // 913:mihailhighfantasypack.esp
  {1000,12,30,10,12,true}, // 913:mihailhighfantasypack.esp
  {1000,12,30,80,30,true}, // 913:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // a36:mihailhighfantasypack.esp
  {1,0,0,3,1,false}, // a36:mihailhighfantasypack.esp
  {1,0,0,10,1,false}, // a36:mihailhighfantasypack.esp
  {1,0,0,80,1,false}, // a36:mihailhighfantasypack.esp
  {1000,30,50,1,30,true}, // d3b:mihailhighfantasypack.esp
  {1000,30,50,3,30,true}, // d3b:mihailhighfantasypack.esp
  {1000,30,50,10,30,true}, // d3b:mihailhighfantasypack.esp
  {1000,30,50,80,50,true}, // d3b:mihailhighfantasypack.esp
  {1000,5,10,1,5,true}, // b41:mihailhighfantasypack.esp
  {1000,5,10,3,5,true}, // b41:mihailhighfantasypack.esp
  {1000,5,10,10,10,true}, // b41:mihailhighfantasypack.esp
  {1000,5,10,80,10,true}, // b41:mihailhighfantasypack.esp
  {1000,4,10,1,4,true}, // 8a6:mihailhighfantasypack.esp
  {1000,4,10,3,4,true}, // 8a6:mihailhighfantasypack.esp
  {1000,4,10,10,10,true}, // 8a6:mihailhighfantasypack.esp
  {1000,4,10,80,10,true}, // 8a6:mihailhighfantasypack.esp
  {1000,6,12,1,6,true}, // 8a7:mihailhighfantasypack.esp
  {1000,6,12,3,6,true}, // 8a7:mihailhighfantasypack.esp
  {1000,6,12,10,10,true}, // 8a7:mihailhighfantasypack.esp
  {1000,6,12,80,12,true}, // 8a7:mihailhighfantasypack.esp
  {1000,8,15,1,8,true}, // 8a8:mihailhighfantasypack.esp
  {1000,8,15,3,8,true}, // 8a8:mihailhighfantasypack.esp
  {1000,8,15,10,10,true}, // 8a8:mihailhighfantasypack.esp
  {1000,8,15,80,15,true}, // 8a8:mihailhighfantasypack.esp
  {1000,4,10,1,4,true}, // 8a5:mihailhighfantasypack.esp
  {1000,4,10,3,4,true}, // 8a5:mihailhighfantasypack.esp
  {1000,4,10,10,10,true}, // 8a5:mihailhighfantasypack.esp
  {1000,4,10,80,10,true}, // 8a5:mihailhighfantasypack.esp
  {1,10,0,1,1,false}, // 5904:mihailhighfantasypack.esp
  {1,10,0,3,1,false}, // 5904:mihailhighfantasypack.esp
  {1,10,0,10,1,false}, // 5904:mihailhighfantasypack.esp
  {1,10,0,80,1,false}, // 5904:mihailhighfantasypack.esp
  {1000,13,20,1,13,true}, // 8a9:mihailhighfantasypack.esp
  {1000,13,20,3,13,true}, // 8a9:mihailhighfantasypack.esp
  {1000,13,20,10,13,true}, // 8a9:mihailhighfantasypack.esp
  {1000,13,20,80,20,true}, // 8a9:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // c85:mihailhighfantasypack.esp
  {1,0,0,3,1,false}, // c85:mihailhighfantasypack.esp
  {1,0,0,10,1,false}, // c85:mihailhighfantasypack.esp
  {1,0,0,80,1,false}, // c85:mihailhighfantasypack.esp
  {1000,12,42,1,12,true}, // 9cd:mihailhighfantasypack.esp
  {1000,12,42,3,12,true}, // 9cd:mihailhighfantasypack.esp
  {1000,12,42,10,12,true}, // 9cd:mihailhighfantasypack.esp
  {1000,12,42,80,42,true}, // 9cd:mihailhighfantasypack.esp
  {1,0,100,1,1,false}, // 2d1fd:Skyrim.esm
  {1,0,100,3,1,false}, // 2d1fd:Skyrim.esm
  {1,0,100,10,1,false}, // 2d1fd:Skyrim.esm
  {1,0,100,80,1,false}, // 2d1fd:Skyrim.esm
  {1,0,100,1,1,false}, // 2d1de:Skyrim.esm
  {1,0,100,3,1,false}, // 2d1de:Skyrim.esm
  {1,0,100,10,1,false}, // 2d1de:Skyrim.esm
  {1,0,100,80,1,false}, // 2d1de:Skyrim.esm
  {1000,12,38,1,12,true}, // 927:mihailhighfantasypack.esp
  {1000,12,38,3,12,true}, // 927:mihailhighfantasypack.esp
  {1000,12,38,10,12,true}, // 927:mihailhighfantasypack.esp
  {1000,12,38,80,38,true}, // 927:mihailhighfantasypack.esp
  {1000,1,0,1,1,true}, // 23ab5:Skyrim.esm
  {1000,1,0,3,3,true}, // 23ab5:Skyrim.esm
  {1000,1,0,10,10,true}, // 23ab5:Skyrim.esm
  {1000,1,0,80,80,true}, // 23ab5:Skyrim.esm
  {1,10,0,1,1,false}, // ac2:mihailhighfantasypack.esp
  {1,10,0,3,1,false}, // ac2:mihailhighfantasypack.esp
  {1,10,0,10,1,false}, // ac2:mihailhighfantasypack.esp
  {1,10,0,80,1,false}, // ac2:mihailhighfantasypack.esp
  {1000,15,0,1,15,true}, // 73fbe:Skyrim.esm
  {1000,15,0,3,15,true}, // 73fbe:Skyrim.esm
  {1000,15,0,10,15,true}, // 73fbe:Skyrim.esm
  {1000,15,0,80,80,true}, // 73fbe:Skyrim.esm
  {3,0,0,1,3,false}, // 23a90:Skyrim.esm
  {3,0,0,3,3,false}, // 23a90:Skyrim.esm
  {3,0,0,10,3,false}, // 23a90:Skyrim.esm
  {3,0,0,80,3,false}, // 23a90:Skyrim.esm
  {6,0,0,1,6,false}, // 10fba:HearthFires.esm
  {6,0,0,3,6,false}, // 10fba:HearthFires.esm
  {6,0,0,10,6,false}, // 10fba:HearthFires.esm
  {6,0,0,80,6,false}, // 10fba:HearthFires.esm
  {4,0,0,1,4,false}, // 68d6b:Skyrim.esm
  {4,0,0,3,4,false}, // 68d6b:Skyrim.esm
  {4,0,0,10,4,false}, // 68d6b:Skyrim.esm
  {4,0,0,80,4,false}, // 68d6b:Skyrim.esm
  {1000,4,15,1,4,true}, // 8d8:mihailhighfantasypack.esp
  {1000,4,15,3,4,true}, // 8d8:mihailhighfantasypack.esp
  {1000,4,15,10,10,true}, // 8d8:mihailhighfantasypack.esp
  {1000,4,15,80,15,true}, // 8d8:mihailhighfantasypack.esp
  {1000,7,20,1,7,true}, // 5907:mihailhighfantasypack.esp
  {1000,7,20,3,7,true}, // 5907:mihailhighfantasypack.esp
  {1000,7,20,10,10,true}, // 5907:mihailhighfantasypack.esp
  {1000,7,20,80,20,true}, // 5907:mihailhighfantasypack.esp
  {1000,12,32,1,12,true}, // 948:mihailhighfantasypack.esp
  {1000,12,32,3,12,true}, // 948:mihailhighfantasypack.esp
  {1000,12,32,10,12,true}, // 948:mihailhighfantasypack.esp
  {1000,12,32,80,32,true}, // 948:mihailhighfantasypack.esp
  {1000,10,25,1,10,true}, // 924:mihailhighfantasypack.esp
  {1000,10,25,3,10,true}, // 924:mihailhighfantasypack.esp
  {1000,10,25,10,10,true}, // 924:mihailhighfantasypack.esp
  {1000,10,25,80,25,true}, // 924:mihailhighfantasypack.esp
  {1000,25,40,1,25,true}, // 51877:mihailhighfantasypack.esp
  {1000,25,40,3,25,true}, // 51877:mihailhighfantasypack.esp
  {1000,25,40,10,25,true}, // 51877:mihailhighfantasypack.esp
  {1000,25,40,80,40,true}, // 51877:mihailhighfantasypack.esp
  {1000,4,15,1,4,true}, // 9a0ec:mihailhighfantasypack.esp
  {1000,4,15,3,4,true}, // 9a0ec:mihailhighfantasypack.esp
  {1000,4,15,10,10,true}, // 9a0ec:mihailhighfantasypack.esp
  {1000,4,15,80,15,true}, // 9a0ec:mihailhighfantasypack.esp
  {1,0,100,1,1,false}, // ae6a9:mihailhighfantasypack.esp
  {1,0,100,3,1,false}, // ae6a9:mihailhighfantasypack.esp
  {1,0,100,10,1,false}, // ae6a9:mihailhighfantasypack.esp
  {1,0,100,80,1,false}, // ae6a9:mihailhighfantasypack.esp
  {1000,4,10,1,4,true}, // 9a0e7:mihailhighfantasypack.esp
  {1000,4,10,3,4,true}, // 9a0e7:mihailhighfantasypack.esp
  {1000,4,10,10,10,true}, // 9a0e7:mihailhighfantasypack.esp
  {1000,4,10,80,10,true}, // 9a0e7:mihailhighfantasypack.esp
  {1000,16,22,1,16,true}, // 9a0fe:mihailhighfantasypack.esp
  {1000,16,22,3,16,true}, // 9a0fe:mihailhighfantasypack.esp
  {1000,16,22,10,16,true}, // 9a0fe:mihailhighfantasypack.esp
  {1000,16,22,80,22,true}, // 9a0fe:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // a27:mihailhighfantasypack.esp
  {1,0,0,3,1,false}, // a27:mihailhighfantasypack.esp
  {1,0,0,10,1,false}, // a27:mihailhighfantasypack.esp
  {1,0,0,80,1,false}, // a27:mihailhighfantasypack.esp
  {1000,7,17,1,7,true}, // 831:mihailhighfantasypack.esp
  {1000,7,17,3,7,true}, // 831:mihailhighfantasypack.esp
  {1000,7,17,10,10,true}, // 831:mihailhighfantasypack.esp
  {1000,7,17,80,17,true}, // 831:mihailhighfantasypack.esp
  {1000,6,14,1,6,true}, // 29041:mihailhighfantasypack.esp
  {1000,6,14,3,6,true}, // 29041:mihailhighfantasypack.esp
  {1000,6,14,10,10,true}, // 29041:mihailhighfantasypack.esp
  {1000,6,14,80,14,true}, // 29041:mihailhighfantasypack.esp
  {1,20,0,1,1,false}, // 43be3:Skyrim.esm
  {1,20,0,3,1,false}, // 43be3:Skyrim.esm
  {1,20,0,10,1,false}, // 43be3:Skyrim.esm
  {1,20,0,80,1,false}, // 43be3:Skyrim.esm
  {1,0,0,1,1,false}, // 6dc9d:Skyrim.esm
  {1,0,0,3,1,false}, // 6dc9d:Skyrim.esm
  {1,0,0,10,1,false}, // 6dc9d:Skyrim.esm
  {1,0,0,80,1,false}, // 6dc9d:Skyrim.esm
  {1000,4,10,1,4,true}, // fb5b:mihailhighfantasypack.esp
  {1000,4,10,3,4,true}, // fb5b:mihailhighfantasypack.esp
  {1000,4,10,10,10,true}, // fb5b:mihailhighfantasypack.esp
  {1000,4,10,80,10,true}, // fb5b:mihailhighfantasypack.esp
  {1000,8,15,1,8,true}, // fb5d:mihailhighfantasypack.esp
  {1000,8,15,3,8,true}, // fb5d:mihailhighfantasypack.esp
  {1000,8,15,10,10,true}, // fb5d:mihailhighfantasypack.esp
  {1000,8,15,80,15,true}, // fb5d:mihailhighfantasypack.esp
  {1000,6,12,1,6,true}, // fb5c:mihailhighfantasypack.esp
  {1000,6,12,3,6,true}, // fb5c:mihailhighfantasypack.esp
  {1000,6,12,10,10,true}, // fb5c:mihailhighfantasypack.esp
  {1000,6,12,80,12,true}, // fb5c:mihailhighfantasypack.esp
  {1000,30,70,1,30,true}, // 6ad8e:mihailhighfantasypack.esp
  {1000,30,70,3,30,true}, // 6ad8e:mihailhighfantasypack.esp
  {1000,30,70,10,30,true}, // 6ad8e:mihailhighfantasypack.esp
  {1000,30,70,80,70,true}, // 6ad8e:mihailhighfantasypack.esp
  {1000,13,20,1,13,true}, // fb5e:mihailhighfantasypack.esp
  {1000,13,20,3,13,true}, // fb5e:mihailhighfantasypack.esp
  {1000,13,20,10,13,true}, // fb5e:mihailhighfantasypack.esp
  {1000,13,20,80,20,true}, // fb5e:mihailhighfantasypack.esp
  {1000,25,40,1,25,true}, // 7f285:mihailhighfantasypack.esp
  {1000,25,40,3,25,true}, // 7f285:mihailhighfantasypack.esp
  {1000,25,40,10,25,true}, // 7f285:mihailhighfantasypack.esp
  {1000,25,40,80,40,true}, // 7f285:mihailhighfantasypack.esp
  {1000,20,40,1,20,true}, // 91f:mihailhighfantasypack.esp
  {1000,20,40,3,20,true}, // 91f:mihailhighfantasypack.esp
  {1000,20,40,10,20,true}, // 91f:mihailhighfantasypack.esp
  {1000,20,40,80,40,true}, // 91f:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // 2ebe2:Skyrim.esm
  {1,0,0,3,1,false}, // 2ebe2:Skyrim.esm
  {1,0,0,10,1,false}, // 2ebe2:Skyrim.esm
  {1,0,0,80,1,false}, // 2ebe2:Skyrim.esm
  {1000,13,23,1,13,true}, // 824:mihailhighfantasypack.esp
  {1000,13,23,3,13,true}, // 824:mihailhighfantasypack.esp
  {1000,13,23,10,13,true}, // 824:mihailhighfantasypack.esp
  {1000,13,23,80,23,true}, // 824:mihailhighfantasypack.esp
  {1000,6,15,1,6,true}, // 2903e:mihailhighfantasypack.esp
  {1000,6,15,3,6,true}, // 2903e:mihailhighfantasypack.esp
  {1000,6,15,10,10,true}, // 2903e:mihailhighfantasypack.esp
  {1000,6,15,80,15,true}, // 2903e:mihailhighfantasypack.esp
  {10,0,0,1,10,false}, // 3deee:Skyrim.esm
  {10,0,0,3,10,false}, // 3deee:Skyrim.esm
  {10,0,0,10,10,false}, // 3deee:Skyrim.esm
  {10,0,0,80,10,false}, // 3deee:Skyrim.esm
  {14,14,0,1,14,false}, // 1e60d:Skyrim.esm
  {14,14,0,3,14,false}, // 1e60d:Skyrim.esm
  {14,14,0,10,14,false}, // 1e60d:Skyrim.esm
  {14,14,0,80,14,false}, // 1e60d:Skyrim.esm
  {1000,8,15,1,8,true}, // 894:mihailhighfantasypack.esp
  {1000,8,15,3,8,true}, // 894:mihailhighfantasypack.esp
  {1000,8,15,10,10,true}, // 894:mihailhighfantasypack.esp
  {1000,8,15,80,15,true}, // 894:mihailhighfantasypack.esp
  {1000,4,10,1,4,true}, // 892:mihailhighfantasypack.esp
  {1000,4,10,3,4,true}, // 892:mihailhighfantasypack.esp
  {1000,4,10,10,10,true}, // 892:mihailhighfantasypack.esp
  {1000,4,10,80,10,true}, // 892:mihailhighfantasypack.esp
  {1000,13,20,1,13,true}, // 895:mihailhighfantasypack.esp
  {1000,13,20,3,13,true}, // 895:mihailhighfantasypack.esp
  {1000,13,20,10,13,true}, // 895:mihailhighfantasypack.esp
  {1000,13,20,80,20,true}, // 895:mihailhighfantasypack.esp
  {1000,15,45,1,15,true}, // 969:mihailhighfantasypack.esp
  {1000,15,45,3,15,true}, // 969:mihailhighfantasypack.esp
  {1000,15,45,10,15,true}, // 969:mihailhighfantasypack.esp
  {1000,15,45,80,45,true}, // 969:mihailhighfantasypack.esp
  {0,2,0,1,0,false}, // 4d366:Skyrim.esm
  {0,2,0,3,0,false}, // 4d366:Skyrim.esm
  {0,2,0,10,0,false}, // 4d366:Skyrim.esm
  {0,2,0,80,0,false}, // 4d366:Skyrim.esm
  {1,0,0,1,1,false}, // f820f:Skyrim.esm
  {1,0,0,3,1,false}, // f820f:Skyrim.esm
  {1,0,0,10,1,false}, // f820f:Skyrim.esm
  {1,0,0,80,1,false}, // f820f:Skyrim.esm
  {1000,9,25,1,9,true}, // aa2c:mihailhighfantasypack.esp
  {1000,9,25,3,9,true}, // aa2c:mihailhighfantasypack.esp
  {1000,9,25,10,10,true}, // aa2c:mihailhighfantasypack.esp
  {1000,9,25,80,25,true}, // aa2c:mihailhighfantasypack.esp
  {1000,10,15,1,10,true}, // 1ee92:mihailhighfantasypack.esp
  {1000,10,15,3,10,true}, // 1ee92:mihailhighfantasypack.esp
  {1000,10,15,10,10,true}, // 1ee92:mihailhighfantasypack.esp
  {1000,10,15,80,15,true}, // 1ee92:mihailhighfantasypack.esp
  {1000,20,50,1,20,true}, // 8fa:mihailhighfantasypack.esp
  {1000,20,50,3,20,true}, // 8fa:mihailhighfantasypack.esp
  {1000,20,50,10,20,true}, // 8fa:mihailhighfantasypack.esp
  {1000,20,50,80,50,true}, // 8fa:mihailhighfantasypack.esp
  {1000,5,10,1,5,true}, // b21:mihailhighfantasypack.esp
  {1000,5,10,3,5,true}, // b21:mihailhighfantasypack.esp
  {1000,5,10,10,10,true}, // b21:mihailhighfantasypack.esp
  {1000,5,10,80,10,true}, // b21:mihailhighfantasypack.esp
  {40,10,0,1,40,false}, // aad:mihailhighfantasypack.esp
  {40,10,0,3,40,false}, // aad:mihailhighfantasypack.esp
  {40,10,0,10,40,false}, // aad:mihailhighfantasypack.esp
  {40,10,0,80,40,false}, // aad:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // a38:mihailhighfantasypack.esp
  {1,0,0,3,1,false}, // a38:mihailhighfantasypack.esp
  {1,0,0,10,1,false}, // a38:mihailhighfantasypack.esp
  {1,0,0,80,1,false}, // a38:mihailhighfantasypack.esp
  {1000,1,0,1,1,true}, // 23ab6:Skyrim.esm
  {1000,1,0,3,3,true}, // 23ab6:Skyrim.esm
  {1000,1,0,10,10,true}, // 23ab6:Skyrim.esm
  {1000,1,0,80,80,true}, // 23ab6:Skyrim.esm
  {1000,6,15,1,6,true}, // b9b:mihailhighfantasypack.esp
  {1000,6,15,3,6,true}, // b9b:mihailhighfantasypack.esp
  {1000,6,15,10,10,true}, // b9b:mihailhighfantasypack.esp
  {1000,6,15,80,15,true}, // b9b:mihailhighfantasypack.esp
  {1000,12,42,1,12,true}, // 9c9:mihailhighfantasypack.esp
  {1000,12,42,3,12,true}, // 9c9:mihailhighfantasypack.esp
  {1000,12,42,10,12,true}, // 9c9:mihailhighfantasypack.esp
  {1000,12,42,80,42,true}, // 9c9:mihailhighfantasypack.esp
  {1000,5,10,1,5,true}, // 909:mihailhighfantasypack.esp
  {1000,5,10,3,5,true}, // 909:mihailhighfantasypack.esp
  {1000,5,10,10,10,true}, // 909:mihailhighfantasypack.esp
  {1000,5,10,80,10,true}, // 909:mihailhighfantasypack.esp
  {1000,7,12,1,7,true}, // bab:mihailhighfantasypack.esp
  {1000,7,12,3,7,true}, // bab:mihailhighfantasypack.esp
  {1000,7,12,10,10,true}, // bab:mihailhighfantasypack.esp
  {1000,7,12,80,12,true}, // bab:mihailhighfantasypack.esp
  {1000,12,42,1,12,true}, // 9cb:mihailhighfantasypack.esp
  {1000,12,42,3,12,true}, // 9cb:mihailhighfantasypack.esp
  {1000,12,42,10,12,true}, // 9cb:mihailhighfantasypack.esp
  {1000,12,42,80,42,true}, // 9cb:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // 467bb:Skyrim.esm
  {1,0,0,3,1,false}, // 467bb:Skyrim.esm
  {1,0,0,10,1,false}, // 467bb:Skyrim.esm
  {1,0,0,80,1,false}, // 467bb:Skyrim.esm
  {1,0,0,1,1,false}, // cd6bc:Skyrim.esm
  {1,0,0,3,1,false}, // cd6bc:Skyrim.esm
  {1,0,0,10,1,false}, // cd6bc:Skyrim.esm
  {1,0,0,80,1,false}, // cd6bc:Skyrim.esm
  {1,0,0,1,1,false}, // 1098a1:Skyrim.esm
  {1,0,0,3,1,false}, // 1098a1:Skyrim.esm
  {1,0,0,10,1,false}, // 1098a1:Skyrim.esm
  {1,0,0,80,1,false}, // 1098a1:Skyrim.esm
  {13,0,0,1,13,false}, // 9f3:mihailhighfantasypack.esp
  {13,0,0,3,13,false}, // 9f3:mihailhighfantasypack.esp
  {13,0,0,10,13,false}, // 9f3:mihailhighfantasypack.esp
  {13,0,0,80,13,false}, // 9f3:mihailhighfantasypack.esp
  {1,1,5,1,1,false}, // bd4:mihailhighfantasypack.esp
  {1,1,5,3,1,false}, // bd4:mihailhighfantasypack.esp
  {1,1,5,10,1,false}, // bd4:mihailhighfantasypack.esp
  {1,1,5,80,1,false}, // bd4:mihailhighfantasypack.esp
  {1000,50,70,1,50,true}, // 9d87a:mihailhighfantasypack.esp
  {1000,50,70,3,50,true}, // 9d87a:mihailhighfantasypack.esp
  {1000,50,70,10,50,true}, // 9d87a:mihailhighfantasypack.esp
  {1000,50,70,80,70,true}, // 9d87a:mihailhighfantasypack.esp
  {1000,8,15,1,8,true}, // 868:mihailhighfantasypack.esp
  {1000,8,15,3,8,true}, // 868:mihailhighfantasypack.esp
  {1000,8,15,10,10,true}, // 868:mihailhighfantasypack.esp
  {1000,8,15,80,15,true}, // 868:mihailhighfantasypack.esp
  {1000,12,42,1,12,true}, // 9af:mihailhighfantasypack.esp
  {1000,12,42,3,12,true}, // 9af:mihailhighfantasypack.esp
  {1000,12,42,10,12,true}, // 9af:mihailhighfantasypack.esp
  {1000,12,42,80,42,true}, // 9af:mihailhighfantasypack.esp
  {1,10,0,1,1,false}, // abe:mihailhighfantasypack.esp
  {1,10,0,3,1,false}, // abe:mihailhighfantasypack.esp
  {1,10,0,10,1,false}, // abe:mihailhighfantasypack.esp
  {1,10,0,80,1,false}, // abe:mihailhighfantasypack.esp
  {1,10,0,1,1,false}, // ac4:mihailhighfantasypack.esp
  {1,10,0,3,1,false}, // ac4:mihailhighfantasypack.esp
  {1,10,0,10,1,false}, // ac4:mihailhighfantasypack.esp
  {1,10,0,80,1,false}, // ac4:mihailhighfantasypack.esp
  {1,0,100,1,1,false}, // 84421:mihailhighfantasypack.esp
  {1,0,100,3,1,false}, // 84421:mihailhighfantasypack.esp
  {1,0,100,10,1,false}, // 84421:mihailhighfantasypack.esp
  {1,0,100,80,1,false}, // 84421:mihailhighfantasypack.esp
  {1000,16,26,1,16,true}, // 93e:mihailhighfantasypack.esp
  {1000,16,26,3,16,true}, // 93e:mihailhighfantasypack.esp
  {1000,16,26,10,16,true}, // 93e:mihailhighfantasypack.esp
  {1000,16,26,80,26,true}, // 93e:mihailhighfantasypack.esp
  {1000,2,10,1,2,true}, // 5c0db:mihailhighfantasypack.esp
  {1000,2,10,3,3,true}, // 5c0db:mihailhighfantasypack.esp
  {1000,2,10,10,10,true}, // 5c0db:mihailhighfantasypack.esp
  {1000,2,10,80,10,true}, // 5c0db:mihailhighfantasypack.esp
  {1000,2,10,1,2,true}, // 5c0d7:mihailhighfantasypack.esp
  {1000,2,10,3,3,true}, // 5c0d7:mihailhighfantasypack.esp
  {1000,2,10,10,10,true}, // 5c0d7:mihailhighfantasypack.esp
  {1000,2,10,80,10,true}, // 5c0d7:mihailhighfantasypack.esp
  {1000,9,20,1,9,true}, // 5c0d9:mihailhighfantasypack.esp
  {1000,9,20,3,9,true}, // 5c0d9:mihailhighfantasypack.esp
  {1000,9,20,10,10,true}, // 5c0d9:mihailhighfantasypack.esp
  {1000,9,20,80,20,true}, // 5c0d9:mihailhighfantasypack.esp
  {1000,6,15,1,6,true}, // 5c0de:mihailhighfantasypack.esp
  {1000,6,15,3,6,true}, // 5c0de:mihailhighfantasypack.esp
  {1000,6,15,10,10,true}, // 5c0de:mihailhighfantasypack.esp
  {1000,6,15,80,15,true}, // 5c0de:mihailhighfantasypack.esp
  {1000,12,22,1,12,true}, // 5c0e0:mihailhighfantasypack.esp
  {1000,12,22,3,12,true}, // 5c0e0:mihailhighfantasypack.esp
  {1000,12,22,10,12,true}, // 5c0e0:mihailhighfantasypack.esp
  {1000,12,22,80,22,true}, // 5c0e0:mihailhighfantasypack.esp
  {1000,12,42,1,12,true}, // 9ca:mihailhighfantasypack.esp
  {1000,12,42,3,12,true}, // 9ca:mihailhighfantasypack.esp
  {1000,12,42,10,12,true}, // 9ca:mihailhighfantasypack.esp
  {1000,12,42,80,42,true}, // 9ca:mihailhighfantasypack.esp
  {500,1,0,1,1,true}, // 23a91:Skyrim.esm
  {500,1,0,3,1,true}, // 23a91:Skyrim.esm
  {500,1,0,10,5,true}, // 23a91:Skyrim.esm
  {500,1,0,80,40,true}, // 23a91:Skyrim.esm
  {1000,15,22,1,15,true}, // 93a:mihailhighfantasypack.esp
  {1000,15,22,3,15,true}, // 93a:mihailhighfantasypack.esp
  {1000,15,22,10,15,true}, // 93a:mihailhighfantasypack.esp
  {1000,15,22,80,22,true}, // 93a:mihailhighfantasypack.esp
  {1,10,0,1,1,false}, // ac9:mihailhighfantasypack.esp
  {1,10,0,3,1,false}, // ac9:mihailhighfantasypack.esp
  {1,10,0,10,1,false}, // ac9:mihailhighfantasypack.esp
  {1,10,0,80,1,false}, // ac9:mihailhighfantasypack.esp
  {1000,10,15,1,10,true}, // 3839c:mihailhighfantasypack.esp
  {1000,10,15,3,10,true}, // 3839c:mihailhighfantasypack.esp
  {1000,10,15,10,10,true}, // 3839c:mihailhighfantasypack.esp
  {1000,10,15,80,15,true}, // 3839c:mihailhighfantasypack.esp
  {1000,9,13,1,9,true}, // 92d:mihailhighfantasypack.esp
  {1000,9,13,3,9,true}, // 92d:mihailhighfantasypack.esp
  {1000,9,13,10,10,true}, // 92d:mihailhighfantasypack.esp
  {1000,9,13,80,13,true}, // 92d:mihailhighfantasypack.esp
  {1000,20,40,1,20,true}, // 425a4:mihailhighfantasypack.esp
  {1000,20,40,3,20,true}, // 425a4:mihailhighfantasypack.esp
  {1000,20,40,10,20,true}, // 425a4:mihailhighfantasypack.esp
  {1000,20,40,80,40,true}, // 425a4:mihailhighfantasypack.esp
  {1000,30,50,1,30,true}, // d9a:mihailhighfantasypack.esp
  {1000,30,50,3,30,true}, // d9a:mihailhighfantasypack.esp
  {1000,30,50,10,30,true}, // d9a:mihailhighfantasypack.esp
  {1000,30,50,80,50,true}, // d9a:mihailhighfantasypack.esp
  {20,0,0,1,20,false}, // 9f6:mihailhighfantasypack.esp
  {20,0,0,3,20,false}, // 9f6:mihailhighfantasypack.esp
  {20,0,0,10,20,false}, // 9f6:mihailhighfantasypack.esp
  {20,0,0,80,20,false}, // 9f6:mihailhighfantasypack.esp
  {1000,30,50,1,30,true}, // d96:mihailhighfantasypack.esp
  {1000,30,50,3,30,true}, // d96:mihailhighfantasypack.esp
  {1000,30,50,10,30,true}, // d96:mihailhighfantasypack.esp
  {1000,30,50,80,50,true}, // d96:mihailhighfantasypack.esp
  {1000,50,70,1,50,true}, // 9d87c:mihailhighfantasypack.esp
  {1000,50,70,3,50,true}, // 9d87c:mihailhighfantasypack.esp
  {1000,50,70,10,50,true}, // 9d87c:mihailhighfantasypack.esp
  {1000,50,70,80,70,true}, // 9d87c:mihailhighfantasypack.esp
  {1000,12,42,1,12,true}, // 9c8:mihailhighfantasypack.esp
  {1000,12,42,3,12,true}, // 9c8:mihailhighfantasypack.esp
  {1000,12,42,10,12,true}, // 9c8:mihailhighfantasypack.esp
  {1000,12,42,80,42,true}, // 9c8:mihailhighfantasypack.esp
  {1000,10,25,1,10,true}, // 14c41:mihailhighfantasypack.esp
  {1000,10,25,3,10,true}, // 14c41:mihailhighfantasypack.esp
  {1000,10,25,10,10,true}, // 14c41:mihailhighfantasypack.esp
  {1000,10,25,80,25,true}, // 14c41:mihailhighfantasypack.esp
  {1000,6,20,1,6,true}, // c9f:mihailhighfantasypack.esp
  {1000,6,20,3,6,true}, // c9f:mihailhighfantasypack.esp
  {1000,6,20,10,10,true}, // c9f:mihailhighfantasypack.esp
  {1000,6,20,80,20,true}, // c9f:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // a1b:mihailhighfantasypack.esp
  {1,0,0,3,1,false}, // a1b:mihailhighfantasypack.esp
  {1,0,0,10,1,false}, // a1b:mihailhighfantasypack.esp
  {1,0,0,80,1,false}, // a1b:mihailhighfantasypack.esp
  {1000,12,42,1,12,true}, // 9cc:mihailhighfantasypack.esp
  {1000,12,42,3,12,true}, // 9cc:mihailhighfantasypack.esp
  {1000,12,42,10,12,true}, // 9cc:mihailhighfantasypack.esp
  {1000,12,42,80,42,true}, // 9cc:mihailhighfantasypack.esp
  {1000,50,70,1,50,true}, // 9d87b:mihailhighfantasypack.esp
  {1000,50,70,3,50,true}, // 9d87b:mihailhighfantasypack.esp
  {1000,50,70,10,50,true}, // 9d87b:mihailhighfantasypack.esp
  {1000,50,70,80,70,true}, // 9d87b:mihailhighfantasypack.esp
  {1000,10,20,1,10,true}, // 2e1a9:mihailhighfantasypack.esp
  {1000,10,20,3,10,true}, // 2e1a9:mihailhighfantasypack.esp
  {1000,10,20,10,10,true}, // 2e1a9:mihailhighfantasypack.esp
  {1000,10,20,80,20,true}, // 2e1a9:mihailhighfantasypack.esp
  {1000,40,80,1,40,true}, // 74fad:mihailhighfantasypack.esp
  {1000,40,80,3,40,true}, // 74fad:mihailhighfantasypack.esp
  {1000,40,80,10,40,true}, // 74fad:mihailhighfantasypack.esp
  {1000,40,80,80,80,true}, // 74fad:mihailhighfantasypack.esp
  {50,10,0,1,50,false}, // 1ee12:mihailhighfantasypack.esp
  {50,10,0,3,50,false}, // 1ee12:mihailhighfantasypack.esp
  {50,10,0,10,50,false}, // 1ee12:mihailhighfantasypack.esp
  {50,10,0,80,50,false}, // 1ee12:mihailhighfantasypack.esp
  {1000,5,10,1,5,true}, // b58:mihailhighfantasypack.esp
  {1000,5,10,3,5,true}, // b58:mihailhighfantasypack.esp
  {1000,5,10,10,10,true}, // b58:mihailhighfantasypack.esp
  {1000,5,10,80,10,true}, // b58:mihailhighfantasypack.esp
  {50,10,0,1,50,false}, // 1ee10:mihailhighfantasypack.esp
  {50,10,0,3,50,false}, // 1ee10:mihailhighfantasypack.esp
  {50,10,0,10,50,false}, // 1ee10:mihailhighfantasypack.esp
  {50,10,0,80,50,false}, // 1ee10:mihailhighfantasypack.esp
  {50,10,0,1,50,false}, // 1ee0e:mihailhighfantasypack.esp
  {50,10,0,3,50,false}, // 1ee0e:mihailhighfantasypack.esp
  {50,10,0,10,50,false}, // 1ee0e:mihailhighfantasypack.esp
  {50,10,0,80,50,false}, // 1ee0e:mihailhighfantasypack.esp
  {50,10,0,1,50,false}, // 1ee0f:mihailhighfantasypack.esp
  {50,10,0,3,50,false}, // 1ee0f:mihailhighfantasypack.esp
  {50,10,0,10,50,false}, // 1ee0f:mihailhighfantasypack.esp
  {50,10,0,80,50,false}, // 1ee0f:mihailhighfantasypack.esp
  {1000,30,70,1,30,true}, // 9bd:mihailhighfantasypack.esp
  {1000,30,70,3,30,true}, // 9bd:mihailhighfantasypack.esp
  {1000,30,70,10,30,true}, // 9bd:mihailhighfantasypack.esp
  {1000,30,70,80,70,true}, // 9bd:mihailhighfantasypack.esp
  {1,0,0,1,1,false}, // 6d22e:Skyrim.esm
  {1,0,0,3,1,false}, // 6d22e:Skyrim.esm
  {1,0,0,10,1,false}, // 6d22e:Skyrim.esm
  {1,0,0,80,1,false}, // 6d22e:Skyrim.esm
  {1000,1,0,1,1,true}, // a5600:Skyrim.esm
  {1000,1,0,3,3,true}, // a5600:Skyrim.esm
  {1000,1,0,10,10,true}, // a5600:Skyrim.esm
  {1000,1,0,80,80,true}, // a5600:Skyrim.esm
  {1,0,0,1,1,false}, // a32:mihailhighfantasypack.esp
  {1,0,0,3,1,false}, // a32:mihailhighfantasypack.esp
  {1,0,0,10,1,false}, // a32:mihailhighfantasypack.esp
  {1,0,0,80,1,false}, // a32:mihailhighfantasypack.esp
};
struct Initial { uint16_t level; std::array<float,3> race; std::array<int16_t,3> offsets; float bonus; std::array<int,3> expected; };
constexpr Initial initials[] = {
  {1, {100.75f,50.9f,50.5f}, {1,-50,0}, 5.9f, {101,0,50}},
  {1, {0.75f,50.0f,50.0f}, {-1,0,0}, 5.5f, {-1,50,50}},
  {1, {50.9f,70.2f,100.99f}, {-30,3,-50}, 5.0f, {20,73,50}},
  {2, {100.75f,50.9f,50.5f}, {1,-50,0}, 5.9f, {106,0,50}},
  {2, {0.75f,50.0f,50.0f}, {-1,0,0}, 5.5f, {4,50,50}},
  {2, {50.9f,70.2f,100.99f}, {-30,3,-50}, 5.0f, {25,73,50}},
  {3, {100.75f,50.9f,50.5f}, {1,-50,0}, 5.9f, {112,0,50}},
  {3, {0.75f,50.0f,50.0f}, {-1,0,0}, 5.5f, {10,50,50}},
  {3, {50.9f,70.2f,100.99f}, {-30,3,-50}, 5.0f, {30,73,50}},
  {12, {100.75f,50.9f,50.5f}, {1,-50,0}, 5.9f, {165,0,50}},
  {12, {0.75f,50.0f,50.0f}, {-1,0,0}, 5.5f, {59,50,50}},
  {12, {50.9f,70.2f,100.99f}, {-30,3,-50}, 5.0f, {75,73,50}},
};
}
