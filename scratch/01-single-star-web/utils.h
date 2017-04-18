#ifndef __UTILS_H_
#define __UTILS_H_

#include <string>
#include <vector>
#include <iostream>

std::vector<std::string> split2vector (const std::string& s, const std::string& delim)
{
  using namespace std;

  vector<string> results;

  string::size_type last_position = 0;

  while(last_position < s.size()) {
    string::size_type hit = s.find(delim, last_position);
    if(hit == string::npos)
      hit = s.size();
    results.push_back(s.substr(last_position, hit - last_position));
    last_position = hit+delim.size();
  }
  
  return results;
}

#endif
