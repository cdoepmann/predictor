#include "pytalk.h"

#include <iostream>
#include <chrono>
#include <string>

#include <boost/process.hpp>
namespace bp = boost::process;

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

PyScript::PyScript(const char *command) : childproc{command, bp::std_out > childout, bp::std_in < childin}
{
}

PyScript::~PyScript()
{
    // childproc.wait_for(std::chrono::seconds{1});
    // std::string s;
    // childout >> s;
    // std::cout << s << std::endl;
}

void PyScript::call_recursive(rapidjson::Writer<rapidjson::StringBuffer> &writer)
{
}

template <>
void PyScript::write_element(rapidjson::Writer<rapidjson::StringBuffer> &writer,
                             int elem)
{
    writer.Int(elem);
}

template <>
void PyScript::write_element(rapidjson::Writer<rapidjson::StringBuffer> &writer,
                             uint16_t elem)
{
    writer.Int(elem);
}

template <>
void PyScript::write_element(rapidjson::Writer<rapidjson::StringBuffer> &writer,
                             const char *elem)
{
    writer.String(elem);
}

template <>
void PyScript::write_element(rapidjson::Writer<rapidjson::StringBuffer> &writer,
                             std::string elem)
{
    writer.String(elem.c_str());
}

template <>
void PyScript::write_element(rapidjson::Writer<rapidjson::StringBuffer> &writer,
                             double elem)
{
    writer.Double(elem);
}