#include "core.hpp"
#include <fstream>
#include <iterator>
#include <iostream>
int main(int argc,char**argv){if(argc<2)return 2;std::ifstream f(argv[1]);std::string s((std::istreambuf_iterator<char>(f)),{});char e[128];bool ok;if(argc>2){spark::Node n;ok=spark::parse_node(s.data(),s.size(),argv[2],n,e,sizeof e);}else{spark::Cache c;ok=spark::parse_list(s.data(),s.size(),c,e,sizeof e);}std::cout<<(ok?"accepted":e)<<'\n';return ok?0:1;}
