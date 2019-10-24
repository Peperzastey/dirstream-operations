#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <string>
#include <iostream>
#include <system_error>
#include <unordered_map>
#include <functional>
#include <limits>
#include <cassert>
#include <type_traits>

enum DirStreamPos {
    BEFORE_FIRST,
    VALID_DIRENT,
    AFTER_LAST,
    NOT_READ    //TODO
};

struct OpContext {
    explicit OpContext(const std::string &dirname)
        : dirname(dirname) {
        dir = opendir(dirname.c_str());
        if(!dir) {
            std::cerr << "Can't open directory.\n";
            throw std::system_error(errno, std::generic_category());
        }
        pos_marker = BEFORE_FIRST;
    }
    ~OpContext() {
        closedir(dir);
    }

    DIR *dir = nullptr;
    dirent *curr_dirent = nullptr;
    const std::string dirname;
    DirStreamPos pos_marker;    //TODO update in all related ops
};

void op_list(OpContext &ctx);
void op_list_rest(OpContext &ctx);
void op_rewind_list(OpContext &ctx);
void op_pos(OpContext &ctx);
void op_next(OpContext &ctx);
void op_tell(OpContext &ctx);
void op_seek(OpContext &ctx);
/// Reset directory stream.
void op_rewind(OpContext &ctx);

void print_dirent(const dirent *dirent);
long do_telldir(DIR *dirstream);

int main(int argc, char *argv[]) try {
    static_cast<void>(argc); // unused parameters
    static_cast<void>(argv);
    /*std::array<const char*, 8> operations = {
        "list",
        "list rest"
        "tell",
        "seek",
        "rewind",
        "rm",
        "mv",
        "exit"
    };
    // dirfd; scandir, scandirat, alphasort, versionsort*/
    std::unordered_map</*const*/std::string, /*const*/std::string> shorts = {
        {"l", "list"},
        {"lr", "listrest"},
        {"rl", "rewindlist"},
        {"p", "pos"},
        {"n", "next"},
        {"t", "tell"},
        {"s", "seek"},
        {"r", "rewind"},
    };
    std::unordered_map</*const*/std::string, std::function<void(OpContext&)>> operations = {
        {"list", op_list},
        {"listrest", op_list_rest},
        {"rewindlist", op_rewind_list},
        {"pos", op_pos},
        {"next", op_next},
        {"tell", op_tell},
        {"seek", op_seek},
        {"rewind", op_rewind},
    };
    using ConstIter = decltype(operations)::const_iterator;

    OpContext opContext(".");
    std::string input;
    while ( std::cout << '>', std::cin >> input ) { //std::getline(std::cin, input)
        if (input == "exit" || input == "q")
            break;
        //TODO max length of input
        const auto shortIt = shorts.find(input);
        if (shortIt != shorts.end())
            input = shortIt->second;
        /*const auto*/
        ConstIter operIt = operations.find(input);
        if (operIt == operations.end())
            std::cout << "No such operation\n";
        else
            operIt->second(opContext);

        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    };

    return 0;
} catch (const std::system_error &ex) {
    std::cerr << "ERROR: " << ex.code() << " : " << ex.what() << '\n';
}

void op_list(OpContext &ctx) {
    DIR *dir = opendir(ctx.dirname.c_str());
    if (!dir) {
        std::cerr << "Can't open directory.\n";
        throw std::system_error(errno, std::generic_category());
    }

    struct dirent *file_ent;
    while (true) {
        file_ent = readdir(dir);
        if (file_ent == NULL)
            break;
        print_dirent(file_ent);
    }
    closedir(dir);
    fprintf(stdout, "\n");
}

void op_list_rest(OpContext &ctx) {
    auto dirstream_pos = telldir(ctx.dir);
    if (dirstream_pos == -1) {
        std::cerr << "Telldir failed.\n";
        throw std::system_error(errno, std::generic_category());
    }

    DIR *dir = opendir(ctx.dirname.c_str());
    if (!dir) {
        std::cerr << "Can't open directory.\n";
        throw std::system_error(errno, std::generic_category());
    }

    // dirstream position does not change when opening a new dirstream for the same dir
    assert(telldir(ctx.dir) == dirstream_pos);
    seekdir(dir, dirstream_pos);
    // dirstream position does not change when changing position in a different dirstream for the same dir
    assert(telldir(ctx.dir) == dirstream_pos);

    struct dirent *file_ent;
    while (true) {
        file_ent = readdir(dir);
        if (file_ent == NULL)
            break;
        print_dirent(file_ent);
    }
    closedir(dir);
    // dirstream position does not change when changing position in a different dirstream for the same dir
    assert(telldir(ctx.dir) == dirstream_pos);

    fprintf(stdout, "\n");
}

void op_rewind_list(OpContext &ctx) {
    auto curr_dirstream_pos = telldir(ctx.dir);
    if (curr_dirstream_pos == -1) {
        std::cerr << "Telldir failed.\n";
        throw std::system_error(errno, std::generic_category());
    }

    rewinddir(ctx.dir);

    struct dirent *file_ent;
    while (true) {
        file_ent = readdir(ctx.dir);
        if (file_ent == NULL)
            break;
        print_dirent(file_ent);
    }
    fprintf(stdout, "\n");

    seekdir(ctx.dir, curr_dirstream_pos);
}

void op_pos(OpContext &ctx) {
    switch (ctx.pos_marker) {
        case VALID_DIRENT:
            assert(ctx.curr_dirent != nullptr);
            print_dirent(ctx.curr_dirent);
            break;
        case BEFORE_FIRST:
            fprintf(stdout, "Before first dirent.\n");
            break;
        case AFTER_LAST:
            fprintf(stdout, "After last dirent.\n");
            break;
        default:
            throw std::logic_error("op_pos logic error");
    }
    fprintf(stdout, "\n");
}

void op_next(OpContext &ctx) {
    auto nextDirent = readdir(ctx.dir);
    if (!nextDirent) {
        fprintf(stdout, "Moved to the end of directory stream.\n");
        ctx.pos_marker = AFTER_LAST;
    } else {
        fprintf(stdout, "Dirent pointer moved to next dirent.\n");
        if (ctx.pos_marker == BEFORE_FIRST)
            ctx.pos_marker = VALID_DIRENT;
        print_dirent(nextDirent);
    }
    ctx.curr_dirent = nextDirent;
    fprintf(stdout, "\n");
}

void op_tell(OpContext &ctx) {
    unsigned long dirstream_pos = do_telldir(ctx.dir); // integral conversion to unsigned
    fprintf(stdout, "%lu", dirstream_pos);
    fprintf(stdout, "\n");
}

void op_seek(OpContext &ctx) {
    // must be valid
    unsigned long position = 0;
    if (!(std::cin >> position)) {
        std::cout << "Wrong operation argument\n";
        return;
    }
    fprintf(stdout, "Pos as signed: %ld\n", reinterpret_cast<long&>(position));
    seekdir(ctx.dir, reinterpret_cast<long&>(position)); // type aliasing
    //TODO check somehow?

    unsigned long dirstream_pos = do_telldir(ctx.dir); // integral conversion to unsigned
    fprintf(stdout, "New position: %lu\n", dirstream_pos);
    fprintf(stdout, "\n");
}

void op_rewind(OpContext &ctx) {
    rewinddir(ctx.dir);
    unsigned long dirstream_pos = do_telldir(ctx.dir); // integral conversion to unsigned
    fprintf(stdout, "New position: %lu\n", dirstream_pos);
    fprintf(stdout, "\n");
}

void print_dirent(const dirent *dirent) {
    using UnsignedOffTRef = const std::make_unsigned_t< decltype(dirent::d_off) >&;

    const char *file_type = "unknown";
    switch (dirent->d_type) {
        case DT_BLK: file_type = "block device"; break;
        case DT_CHR: file_type = "character device"; break;
        case DT_DIR: file_type = "directory"; break;
        case DT_FIFO: file_type = "named pipe"; break;
        case DT_LNK: file_type = "symbolic link"; break;
        case DT_REG: file_type = "regular file"; break;
        case DT_SOCK: file_type = "UNIX domain socket"; break;
        default: ; //DT_UNKNOWN
    }
    fprintf(stdout, "dirent: inode: %lu, \toff: %lu, \treclen: %d, name: %s, \ttype: %s\n",
        dirent->d_ino,
        reinterpret_cast<UnsignedOffTRef>(dirent->d_off), // type aliasing, but integral conversion to UnsignedOffT is enough here
        dirent->d_reclen,
        dirent->d_name,
        file_type
    );
}

long do_telldir(DIR *dirstream) {
    auto dirstream_pos = telldir(dirstream);
    if (dirstream_pos == -1) {
        std::cerr << "Telldir failed.\n";
        throw std::system_error(errno, std::generic_category());
    }

    return dirstream_pos;
}
