// Module for exchanging data between a C++ based application and a Python
// script running as a "child" process

#pragma once

#ifndef _PYTALK_H_
#define _PYTALK_H_

#include <iostream>
// #include <initializer_list>
#include <iterator>
#include <array>
#include <vector>
#include <memory>

#include <boost/process.hpp>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

// the class for communication via JSON

class PyScript
{
  protected:
    boost::process::opstream childin;
    boost::process::ipstream childout;
    boost::process::child childproc;

  public:
    PyScript(const char *command);
    ~PyScript();

    template <typename... Args>
    rapidjson::Document * call(const char *cmd, Args... args);
    
    template <typename... Args>
    void dump(const char *cmd, Args... args);

  private:
    template <typename T, typename... Args>
    void call_recursive(
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        const char *key,
        T value,
        Args... args);

    void call_recursive(
        rapidjson::Writer<rapidjson::StringBuffer> &writer);

    template <typename T>
    void write_element(
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        T elem);

    template <typename T>
    void write_element(
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        std::vector<T> list);
};

template <typename... Args>
void PyScript::dump(const char *cmd, Args... args)
{
    using namespace rapidjson;

    StringBuffer sb;
    Writer<StringBuffer> writer{sb};

    writer.StartObject();

    writer.Key("event");
    writer.String(cmd);

    call_recursive(writer, args...);
    writer.EndObject();

    std::cout << "#<#" << sb.GetString();
    std::cout << "#>#" << std::endl;
}

template <typename... Args>
rapidjson::Document * PyScript::call(const char *cmd, Args... args)
{
    using namespace rapidjson;

    StringBuffer sb;
    Writer<StringBuffer> writer{sb};

    writer.StartObject();

    writer.Key("cmd");
    writer.String(cmd);

    call_recursive(writer, args...);
    writer.EndObject();

    childin << sb.GetString();
    childin << std::endl;

    // read until magic output
    while (true) {
        std::string magic_line;
        std::getline(childout, magic_line);
        
        if (magic_line == "::: PyTalk Output :::") {
            break;
        }
    }

    std::string result;
    std::getline(childout, result);

    Document * d = new Document;
    d->Parse(result.c_str());

    return d;
}

template <typename T, typename... Args>
void PyScript::call_recursive(rapidjson::Writer<rapidjson::StringBuffer> &writer, const char *key, T value, Args... args)
{
    using namespace rapidjson;

    writer.Key(key);
    write_element(writer, value);
    call_recursive(writer, args...);
}

template <typename T>
void PyScript::write_element(rapidjson::Writer<rapidjson::StringBuffer> &writer,
                             std::vector<T> list)
{
    writer.StartArray();
    for (auto elem : list)
    {
        write_element(writer, elem);
    }
    writer.EndArray();
}

#endif