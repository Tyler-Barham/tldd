// tldd: print tree of shared library dependencies

// Copyright (C) 2013 Jonathan Wakely
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <pstream.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <stdexcept>
#include <sstream>
#include <iostream>

struct lib
{
    std::string soname;
    std::string path;
    std::string address;

    std::vector<lib*> dependencies;
};

typedef std::map<std::string, lib> libmap;
typedef std::vector<std::string> strvec;

template<typename F>
    strvec
    read_tag(std::initializer_list<std::string> cmd, F match)
    {
        strvec results;
        strvec words;

        redi::ipstream in(cmd);
        if (!in.is_open())
            throw std::runtime_error(*cmd.begin() + ": " + std::strerror(in.rdbuf()->error()));

        std::string s;
        while (getline(in, s))
        {
            words.clear();
            std::istringstream buf(s);
            while (buf >> s)
                words.push_back(s);
            auto res = match(words);
            if (res.first)
                results.push_back(res.second);
        }
        return results;
    }

std::vector<std::string>
read_needed_readelf(const std::string& path)
{
    return read_tag({"readelf", "-d", path}, [](const strvec& words) {
                if (words.size() == 5 && words[1] == "(NEEDED)" && words[2] == "Shared" && words[3] == "library:")
                    return std::make_pair(true, words[4].substr(1, words[4].length()-2));
                return std::make_pair(false, std::string());
            });
}

std::vector<std::string>
read_soname_readelf(const std::string& path)
{
    return read_tag({"readelf", "-d", path}, [](const strvec& words) {
                if (words.size() == 5 && words[1] == "(SONAME)" && words[2] == "Library" && words[3] == "soname:")
                    return std::make_pair(true, words[4].substr(1, words[4].length()-2));
                return std::make_pair(false, std::string());
            });
}

std::vector<std::string>
read_needed_eu_readelf(const std::string& path)
{
    return read_tag({"eu-readelf", "-d", path}, [](const strvec& words) {
                if (words.size() == 4 && words[0] == "NEEDED" && words[1] == "Shared" && words[2] == "library:")
                    return std::make_pair(true, words[3].substr(1, words[3].length()-2));
                return std::make_pair(false, std::string());
            });
}

std::vector<std::string>
read_soname_eu_readelf(const std::string& path)
{
    return read_tag({"eu-readelf", "-d", path}, [](const strvec& words) {
                if (words.size() == 4 && words[0] == "SONAME" && words[1] == "Library" && words[2] == "soname:")
                    return std::make_pair(true, words[3].substr(1, words[3].length()-2));
                return std::make_pair(false, std::string());
            });
}

std::vector<std::string>
read_needed_elfdump(const std::string& path)
{
    return read_tag({"elfdump", "-d", path}, [](const strvec& words) {
                if (words.size() == 4 && words[1] == "NEEDED")
                    return std::make_pair(true, words[3]);
                return std::make_pair(false, std::string());
            });
}

std::vector<std::string>
read_soname_elfdump(const std::string& path)
{
    return read_tag({"elfdump", "-d", path}, [](const strvec& words) {
                if (words.size() == 4 && words[1] == "SONAME")
                    return std::make_pair(true, words[3]);
                return std::make_pair(false, std::string());
            });
}


#ifdef __sun
std::vector<std::string> (*read_needed)(const std::string& path) = read_needed_elfdump;
std::vector<std::string> (*read_soname)(const std::string& path) = read_soname_elfdump;
#elif defined(USE_ELFUTILS)
std::vector<std::string> (*read_needed)(const std::string& path) = read_needed_eu_readelf;
std::vector<std::string> (*read_soname)(const std::string& path) = read_soname_eu_readelf;
#else
std::vector<std::string> (*read_needed)(const std::string& path) = read_needed_readelf;
std::vector<std::string> (*read_soname)(const std::string& path) = read_soname_readelf;
#endif

void
find_dependencies(lib& l, libmap& libs)
{
    if (l.path.empty() || !l.dependencies.empty())
        return;
    for (auto needed : read_needed(l.path))
    {
        auto& dep = libs.at(needed);
        l.dependencies.push_back(&dep);
        find_dependencies(dep, libs);
    }
}

void print_deps(const lib& l, bool utf8, std::string prefix = {})
{
    const char* continues = utf8 ? u8"\u251c\u2500" : "|_";
    const char* finished = utf8 ? u8"\u2514\u2500" : "\\_";
    const char* parent_continues = utf8 ? u8"\u2502 " : "| ";
    const char* parent_finished = "  ";

    for (auto& d : l.dependencies)
    {
        const bool last = &d == &l.dependencies.back();
        std::cout << prefix << (last ? finished : continues) << d->soname << " => ";
        if (d->address.empty())
            std::cout << "not found";
        else
            std::cout << d->path << ' ' << d->address;
        std::cout << '\n';
        print_deps(*d, utf8, prefix + (last ? parent_finished : parent_continues));
    }
}

void prune(lib& l)
{
    std::set<std::string> seen;

    struct
    {
        std::set<std::string>& seen;

        bool operator()(lib* l)
        {
            if (seen.count(l->soname))
                return true;
            seen.insert(l->soname);
            auto& deps = l->dependencies;
            auto end = std::remove_if(deps.begin(), deps.end(), *this);
            deps.erase(end, deps.end());
            return false;
        }
    } pruner = { seen };

    pruner(&l);
}

std::string usage(const std::string& name)
{
    return "Usage: " + name + " [-fAUh] FILE...";
}

int main(int argc, char** argv)
{
    int arg = 1;
    bool all = false;
    bool utf8 = isatty(STDOUT_FILENO);

    while (arg < argc && argv[arg][0] == '-')
    {
        std::string a = argv[arg++];
        if (a == "-h" || a == "--help")
        {
            std::cout << usage(argv[0]) << "\n"
                   "  -f, --full   allow libraries to be shown more than once\n"
                   "  -A, --ascii  use ASCII line-drawing characters\n"
                   "  -U, --utf8   use UTF-8 line-drawing characters\n"
                   "  -h, --help   display this help and exit\n";
            return EXIT_SUCCESS;
        }
        else if (a == "-f" || a == "--full")
            all = true;
        else if (a == "-U" || a == "--utf8")
            utf8 = true;
        else if (a == "-A" || a == "--ascii")
            utf8 = false;
        else if (a == "--")
            break;
        else
        {
            std::cerr << argv[0] << ": invalid option -- '" << a << "'\n";
            arg = argc;
        }
    }

    if (arg >= argc)
    {
        std::cerr << usage(argv[0]) << '\n';
        return EXIT_FAILURE;
    }

    while (arg < argc)
    {
        libmap libs;

        redi::ipstream in({"ldd", argv[arg]}, redi::pstreams::pstdout|redi::pstreams::pstderr);
        if (!in.is_open())
        {
            std::cerr << argv[0] << ": cannot execute 'ldd'\n";
            return EXIT_FAILURE;
        }

        try
        {
            std::string s;
            while (getline(in, s))
            {
                std::istringstream buf(s);
                lib l;
                if (buf >> l.soname >> s)
                {
                    if (s == "=>")
                    {
                        if (buf >> l.path)
                        {
                            if (l.path == "not")
                                l.path.clear();
                            else
                                buf >> l.address;
                            libs[l.soname] = l;
                        }
                    }
                    else
                    {
                        l.address = s;
                        swap(l.soname, l.path);
                        l.soname = read_soname(l.path).at(0);
                        libs[l.soname] = l;
                    }
                }
            }
            if (in.rdbuf()->exited() && in.rdbuf()->status())
            {
                std::cerr << argv[0] << ": error executing " << in.err().rdbuf();
                return EXIT_FAILURE;
            }

            lib exe;
            exe.path = argv[arg];

            find_dependencies(exe, libs);

            if (!all)
                prune(exe);

            std::cout << exe.path << '\n';
            print_deps(exe, utf8);

            if (++arg < argc) 
                std::cout << "--\n";

        } catch (std::exception const& e) {
            std::cerr << argv[0] << ": error executing child process: " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
