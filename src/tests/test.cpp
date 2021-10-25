/* Copyright (c) Mark Harmstone 2021
 *
 * This file is part of WinBtrfs.
 *
 * WinBtrfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public Licence as published by
 * the Free Software Foundation, either version 3 of the Licence, or
 * (at your option) any later version.
 *
 * WinBtrfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public Licence for more details.
 *
 * You should have received a copy of the GNU Lesser General Public Licence
 * along with WinBtrfs.  If not, see <http://www.gnu.org/licenses/>. */

#include "test.h"
#include <wincon.h>
#include <functional>

using namespace std;

static unique_handle create_file(const u16string_view& path, ACCESS_MASK access, ULONG atts, ULONG share,
                                 ULONG dispo, ULONG options, ULONG_PTR exp_info) {
    NTSTATUS Status;
    HANDLE h;
    IO_STATUS_BLOCK iosb;
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;

    oa.Length = sizeof(oa);
    oa.RootDirectory = nullptr; // FIXME - test

    us.Length = us.MaximumLength = path.length() * sizeof(char16_t);
    us.Buffer = (WCHAR*)path.data();
    oa.ObjectName = &us;

    oa.Attributes = OBJ_CASE_INSENSITIVE; // FIXME - test
    oa.SecurityDescriptor = nullptr; // FIXME - test
    oa.SecurityQualityOfService = nullptr; // FIXME - test(?)

    // FIXME - AllocationSize
    // FIXME - EaBuffer and EaLength

    iosb.Information = 0xdeadbeef;

    Status = NtCreateFile(&h, access, &oa, &iosb, nullptr, atts, share,
                          dispo, options, nullptr, 0);

    if (Status != STATUS_SUCCESS)
        throw ntstatus_error(Status);

    if (iosb.Information != exp_info)
        throw formatted_error("iosb.Information was {}, expected {}", iosb.Information, exp_info);

    return unique_handle(h);
}

static void test(const string& msg, const function<void()>& func) {
    string err;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    try {
        func();
    } catch (const exception& e) {
        err = e.what();
    } catch (...) {
        err = "Uncaught exception.";
    }

    // FIXME - aligned output?

    fmt::print("{}, ", msg);

    auto col = GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);

    if (col)
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), err.empty() ? FOREGROUND_GREEN : (FOREGROUND_RED | FOREGROUND_INTENSITY));

    fmt::print("{}", err.empty() ? "PASS" : "FAIL");

    if (col)
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), csbi.wAttributes);

    if (!err.empty())
        fmt::print(" ({})", err);

    fmt::print("\n");
}

static void exp_status(const function<void()>& func, NTSTATUS Status) {
    try {
        func();
    } catch (const ntstatus_error& e) {
        if (e.Status != Status)
            throw formatted_error("Status was {}, expected {}", ntstatus_to_string(e.Status), ntstatus_to_string(Status));
        else
            return;
    }

    if (Status != STATUS_SUCCESS)
        throw formatted_error("Status was STATUS_SUCCESS, expected {}", ntstatus_to_string(Status));
}

static FILE_BASIC_INFORMATION query_basic_information(HANDLE h) {
    IO_STATUS_BLOCK iosb;
    NTSTATUS Status;
    FILE_BASIC_INFORMATION fbi;

    Status = NtQueryInformationFile(h, &iosb, &fbi, sizeof(fbi), FileBasicInformation);

    if (Status != STATUS_SUCCESS)
        throw ntstatus_error(Status);

    if (iosb.Information != sizeof(FILE_BASIC_INFORMATION))
        throw formatted_error("iosb.Information was {}, expected {}", iosb.Information, sizeof(FILE_BASIC_INFORMATION));

    return fbi;
}

static FILE_STANDARD_INFORMATION query_standard_information(HANDLE h) {
    IO_STATUS_BLOCK iosb;
    NTSTATUS Status;
    FILE_STANDARD_INFORMATION fsi;

    Status = NtQueryInformationFile(h, &iosb, &fsi, sizeof(fsi), FileStandardInformation);

    if (Status != STATUS_SUCCESS)
        throw ntstatus_error(Status);

    if (iosb.Information != sizeof(FILE_STANDARD_INFORMATION))
        throw formatted_error("iosb.Information was {}, expected {}", iosb.Information, sizeof(FILE_STANDARD_INFORMATION));

    return fsi;
}

static OBJECT_BASIC_INFORMATION query_object_basic_information(HANDLE h) {
    NTSTATUS Status;
    OBJECT_BASIC_INFORMATION obi;
    ULONG len;

    Status = NtQueryObject(h, ObjectBasicInformation, &obi, sizeof(obi), &len);

    if (Status != STATUS_SUCCESS)
        throw ntstatus_error(Status);

    if (len != sizeof(obi))
        throw formatted_error("returned length was {}, expected {}", len, sizeof(obi));

    return obi;
}

static u16string query_file_name_information(HANDLE h) {
    IO_STATUS_BLOCK iosb;
    NTSTATUS Status;
    FILE_NAME_INFORMATION fni;

    fni.FileNameLength = 0;

    Status = NtQueryInformationFile(h, &iosb, &fni, sizeof(fni), FileNameInformation);

    if (Status != STATUS_SUCCESS && Status != STATUS_BUFFER_OVERFLOW)
        throw ntstatus_error(Status);

    vector<uint8_t> buf(offsetof(FILE_NAME_INFORMATION, FileName) + fni.FileNameLength);

    auto& fni2 = *reinterpret_cast<FILE_NAME_INFORMATION*>(buf.data());

    fni2.FileNameLength = buf.size() - offsetof(FILE_NAME_INFORMATION, FileName);

    Status = NtQueryInformationFile(h, &iosb, &fni2, buf.size(), FileNameInformation);

    if (Status != STATUS_SUCCESS)
        throw ntstatus_error(Status);

    if (iosb.Information != buf.size())
        throw formatted_error("iosb.Information was {}, expected {}", iosb.Information, buf.size());

    u16string ret;

    ret.resize(fni.FileNameLength / sizeof(char16_t));

    memcpy(ret.data(), fni2.FileName, fni.FileNameLength);

    return ret;
}

static void test_create_file(const u16string& dir) {
    unique_handle h;

    test("Create file", [&]() {
        h = create_file(dir + u"\\file", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Create duplicate file", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\file", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
            }, STATUS_OBJECT_NAME_COLLISION);
        });

        test("Create file differing in case", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\FILE", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
            }, STATUS_OBJECT_NAME_COLLISION);
        });

        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != FILE_ATTRIBUTE_ARCHIVE) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_ARCHIVE",
                                      fbi.FileAttributes);
            }
        });

        test("Check standard information", [&]() {
            auto fsi = query_standard_information(h.get());

            if (fsi.AllocationSize.QuadPart != 0)
                throw formatted_error("AllocationSize was {}, expected 0", fsi.AllocationSize.QuadPart);

            if (fsi.EndOfFile.QuadPart != 0)
                throw formatted_error("EndOfFile was {}, expected 0", fsi.EndOfFile.QuadPart);

            if (fsi.NumberOfLinks != 1)
                throw formatted_error("NumberOfLinks was {}, expected 1", fsi.NumberOfLinks);

            if (fsi.DeletePending)
                throw runtime_error("DeletePending was true, expected false");

            if (fsi.Directory)
                throw runtime_error("Directory was true, expected false");
        });

        test("Check name", [&]() {
            auto fn = query_file_name_information(h.get());

            static const u16string_view ends_with = u"\\file";

            if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                throw runtime_error("Name did not end with \"\\file\".");
        });

        // FIXME - FileAllInformation
        // FIXME - FileAttributeTagInformation
        // FIXME - FileCompressionInformation
        // FIXME - FileEaInformation
        // FIXME - FileInternalInformation
        // FIXME - FileNetworkOpenInformation
        // FIXME - FilePositionInformation
        // FIXME - FileStreamInformation
        // FIXME - FileHardLinkInformation
        // FIXME - FileNormalizedNameInformation
        // FIXME - FileStandardLinkInformation
        // FIXME - FileIdInformation
        // FIXME - FileStatInformation
        // FIXME - FileStatLxInformation
        // FIXME - FileCaseSensitiveInformation
        // FIXME - FileHardLinkFullIdInformation
        // FIXME - FILE_STANDARD_INFORMATION_EX

        test("Check granted access", [&]() {
            auto obi = query_object_basic_information(h.get());

            ACCESS_MASK exp = SYNCHRONIZE | WRITE_OWNER | WRITE_DAC | READ_CONTROL | DELETE |
                              FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES | FILE_DELETE_CHILD |
                              FILE_EXECUTE | FILE_WRITE_EA | FILE_READ_EA | FILE_APPEND_DATA |
                              FILE_WRITE_DATA | FILE_READ_DATA;

            if (obi.GrantedAccess != exp)
                throw formatted_error("granted access was {:x}, expected {:x}", obi.GrantedAccess, exp);
        });

        h.reset();

        test("Open file", [&]() {
            create_file(dir + u"\\file", MAXIMUM_ALLOWED, 0, 0, FILE_OPEN, 0, FILE_OPENED);
        });
    }

    test("Create file (FILE_NON_DIRECTORY_FILE)", [&]() {
        h = create_file(dir + u"\\file2", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, FILE_NON_DIRECTORY_FILE,
                        FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != FILE_ATTRIBUTE_ARCHIVE) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_ARCHIVE",
                                      fbi.FileAttributes);
            }
        });

        test("Check standard information", [&]() {
            auto fsi = query_standard_information(h.get());

            if (fsi.AllocationSize.QuadPart != 0)
                throw formatted_error("AllocationSize was {}, expected 0", fsi.AllocationSize.QuadPart);

            if (fsi.EndOfFile.QuadPart != 0)
                throw formatted_error("EndOfFile was {}, expected 0", fsi.EndOfFile.QuadPart);

            if (fsi.NumberOfLinks != 1)
                throw formatted_error("NumberOfLinks was {}, expected 1", fsi.NumberOfLinks);

            if (fsi.DeletePending)
                throw runtime_error("DeletePending was true, expected false");

            if (fsi.Directory)
                throw runtime_error("Directory was true, expected false");
        });

        h.reset();
    }

    test("Create file (FILE_NON_DIRECTORY_FILE, FILE_ATTRIBUTE_DIRECTORY)", [&]() {
        h = create_file(dir + u"\\file3", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_DIRECTORY, 0, FILE_CREATE,
                        FILE_NON_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != FILE_ATTRIBUTE_ARCHIVE) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_ARCHIVE",
                                      fbi.FileAttributes);
            }
        });

        test("Check standard information", [&]() {
            auto fsi = query_standard_information(h.get());

            if (fsi.AllocationSize.QuadPart != 0)
                throw formatted_error("AllocationSize was {}, expected 0", fsi.AllocationSize.QuadPart);

            if (fsi.EndOfFile.QuadPart != 0)
                throw formatted_error("EndOfFile was {}, expected 0", fsi.EndOfFile.QuadPart);

            if (fsi.NumberOfLinks != 1)
                throw formatted_error("NumberOfLinks was {}, expected 1", fsi.NumberOfLinks);

            if (fsi.DeletePending)
                throw runtime_error("DeletePending was true, expected false");

            if (fsi.Directory)
                throw runtime_error("Directory was true, expected false");
        });

        h.reset();
    }

    test("Create directory (FILE_DIRECTORY_FILE)", [&]() {
        h = create_file(dir + u"\\dir", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE,
                        FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != FILE_ATTRIBUTE_DIRECTORY) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_DIRECTORY",
                                      fbi.FileAttributes);
            }
        });

        test("Check standard information", [&]() {
            auto fsi = query_standard_information(h.get());

            if (fsi.AllocationSize.QuadPart != 0)
                throw formatted_error("AllocationSize was {}, expected 0", fsi.AllocationSize.QuadPart);

            if (fsi.EndOfFile.QuadPart != 0)
                throw formatted_error("EndOfFile was {}, expected 0", fsi.EndOfFile.QuadPart);

            if (fsi.NumberOfLinks != 1)
                throw formatted_error("NumberOfLinks was {}, expected 1", fsi.NumberOfLinks);

            if (fsi.DeletePending)
                throw runtime_error("DeletePending was true, expected false");

            if (!fsi.Directory)
                throw runtime_error("Directory was false, expected true");
        });

        test("Check granted access", [&]() {
            auto obi = query_object_basic_information(h.get());

            ACCESS_MASK exp = SYNCHRONIZE | WRITE_OWNER | WRITE_DAC | READ_CONTROL | DELETE |
                              FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES | FILE_DELETE_CHILD |
                              FILE_EXECUTE | FILE_WRITE_EA | FILE_READ_EA | FILE_APPEND_DATA |
                              FILE_WRITE_DATA | FILE_READ_DATA;

            if (obi.GrantedAccess != exp)
                throw formatted_error("granted access was {:x}, expected {:x}", obi.GrantedAccess, exp);
        });

        h.reset();

        test("Open directory", [&]() {
            create_file(dir + u"\\dir", MAXIMUM_ALLOWED, 0, 0, FILE_OPEN, 0, FILE_OPENED);
        });
    }

    test("Create file (FILE_ATTRIBUTE_DIRECTORY)", [&]() {
        h = create_file(dir + u"\\file4", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_DIRECTORY, 0, FILE_CREATE,
                        0, FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != FILE_ATTRIBUTE_ARCHIVE) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_ARCHIVE",
                                      fbi.FileAttributes);
            }
        });

        test("Check standard information", [&]() {
            auto fsi = query_standard_information(h.get());

            if (fsi.AllocationSize.QuadPart != 0)
                throw formatted_error("AllocationSize was {}, expected 0", fsi.AllocationSize.QuadPart);

            if (fsi.EndOfFile.QuadPart != 0)
                throw formatted_error("EndOfFile was {}, expected 0", fsi.EndOfFile.QuadPart);

            if (fsi.NumberOfLinks != 1)
                throw formatted_error("NumberOfLinks was {}, expected 1", fsi.NumberOfLinks);

            if (fsi.DeletePending)
                throw runtime_error("DeletePending was true, expected false");

            if (fsi.Directory)
                throw runtime_error("Directory was true, expected false");
        });

        h.reset();
    }

    test("Create file (FILE_ATTRIBUTE_HIDDEN)", [&]() {
        h = create_file(dir + u"\\filehidden", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_HIDDEN, 0, FILE_CREATE,
                        0, FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN)) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN",
                                      fbi.FileAttributes);
            }
        });

        h.reset();
    }

    test("Create file (FILE_ATTRIBUTE_READONLY)", [&]() {
        h = create_file(dir + u"\\filero", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_READONLY, 0, FILE_CREATE,
                        0, FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY)) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY",
                                      fbi.FileAttributes);
            }
        });

        h.reset();
    }

    test("Create file (FILE_ATTRIBUTE_SYSTEM)", [&]() {
        h = create_file(dir + u"\\filesys", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_SYSTEM, 0, FILE_CREATE,
                        0, FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_SYSTEM)) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_SYSTEM",
                                      fbi.FileAttributes);
            }
        });

        h.reset();
    }

    test("Create file (FILE_ATTRIBUTE_NORMAL)", [&]() {
        h = create_file(dir + u"\\filenormal", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_NORMAL, 0, FILE_CREATE,
                        0, FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != FILE_ATTRIBUTE_ARCHIVE) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_ARCHIVE",
                                      fbi.FileAttributes);
            }
        });

        h.reset();
    }

    test("Create directory (FILE_ATTRIBUTE_HIDDEN)", [&]() {
        h = create_file(dir + u"\\dirhidden", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_HIDDEN, 0, FILE_CREATE,
                        FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN",
                                      fbi.FileAttributes);
            }
        });

        h.reset();
    }

    test("Create directory (FILE_ATTRIBUTE_READONLY)", [&]() {
        h = create_file(dir + u"\\dirro", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_READONLY, 0, FILE_CREATE,
                        FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY)) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY",
                                      fbi.FileAttributes);
            }
        });

        h.reset();
    }

    test("Create directory (FILE_ATTRIBUTE_SYSTEM)", [&]() {
        h = create_file(dir + u"\\dirsys", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_SYSTEM, 0, FILE_CREATE,
                        FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM)) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM",
                                      fbi.FileAttributes);
            }
        });

        h.reset();
    }

    test("Create directory (FILE_ATTRIBUTE_NORMAL)", [&]() {
        h = create_file(dir + u"\\dirnormal", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_NORMAL, 0, FILE_CREATE,
                        FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != FILE_ATTRIBUTE_DIRECTORY) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_DIRECTORY",
                                      fbi.FileAttributes);
            }
        });

        h.reset();
    }

    test("Create file (FILE_SHARE_READ)", [&]() {
        h = create_file(dir + u"\\fileshareread", FILE_READ_DATA, 0, FILE_SHARE_READ, FILE_CREATE,
                        0, FILE_CREATED);
    });

    if (h) {
        test("Open for read", [&]() {
            create_file(dir + u"\\fileshareread", FILE_READ_DATA, 0, FILE_SHARE_READ, FILE_OPEN,
                        0, FILE_OPENED);
        });

        test("Open for write", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\fileshareread", FILE_WRITE_DATA, 0, FILE_SHARE_READ, FILE_OPEN,
                            0, FILE_OPENED);
            }, STATUS_SHARING_VIOLATION);
        });

        test("Open for delete", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\fileshareread", DELETE, 0, FILE_SHARE_READ, FILE_OPEN,
                            0, FILE_OPENED);
            }, STATUS_SHARING_VIOLATION);
        });

        h.reset();
    }

    test("Create file (FILE_SHARE_WRITE)", [&]() {
        h = create_file(dir + u"\\filesharewrite", FILE_WRITE_DATA, 0, FILE_SHARE_WRITE, FILE_CREATE,
                        0, FILE_CREATED);
    });

    if (h) {
        test("Open for read", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\filesharewrite", FILE_READ_DATA, 0, FILE_SHARE_WRITE, FILE_OPEN,
                            0, FILE_OPENED);
            }, STATUS_SHARING_VIOLATION);
        });

        test("Open for write", [&]() {
            create_file(dir + u"\\filesharewrite", FILE_WRITE_DATA, 0, FILE_SHARE_WRITE, FILE_OPEN,
                        0, FILE_OPENED);
        });

        test("Open for delete", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\filesharewrite", DELETE, 0, FILE_SHARE_WRITE, FILE_OPEN,
                            0, FILE_OPENED);
            }, STATUS_SHARING_VIOLATION);
        });

        h.reset();
    }

    test("Create file (FILE_SHARE_DELETE)", [&]() {
        h = create_file(dir + u"\\filesharedelete", DELETE, 0, FILE_SHARE_DELETE, FILE_CREATE,
                        0, FILE_CREATED);
    });

    if (h) {
        test("Open for read", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\filesharedelete", FILE_READ_DATA, 0, FILE_SHARE_DELETE, FILE_OPEN,
                            0, FILE_OPENED);
            }, STATUS_SHARING_VIOLATION);
        });

        test("Open for write", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\filesharedelete", FILE_WRITE_DATA, 0, FILE_SHARE_DELETE, FILE_OPEN,
                            0, FILE_OPENED);
            }, STATUS_SHARING_VIOLATION);
        });

        test("Open for delete", [&]() {
            create_file(dir + u"\\filesharedelete", DELETE, 0, FILE_SHARE_DELETE, FILE_OPEN,
                        0, FILE_OPENED);
        });

        h.reset();
    }

    test("Create file in invalid path", [&]() {
        exp_status([&]() {
            create_file(dir + u"\\nosuchdir\\file", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, FILE_NON_DIRECTORY_FILE,
                        FILE_CREATED);
        }, STATUS_OBJECT_PATH_NOT_FOUND);
    });

    test("Create directory in invalid path", [&]() {
        exp_status([&]() {
            create_file(dir + u"\\nosuchdir\\file", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE,
                        FILE_CREATED);
        }, STATUS_OBJECT_PATH_NOT_FOUND);
    });

    test("Create file by FILE_SUPERSEDE", [&]() {
        h = create_file(dir + u"\\supersede", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_READONLY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SUPERSEDE, 0, FILE_CREATED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE)) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE",
                                      fbi.FileAttributes);
            }
        });

        test("Try superseding open file", [&]() {
            create_file(dir + u"\\supersede", MAXIMUM_ALLOWED, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SUPERSEDE, 0, FILE_SUPERSEDED);
        });

        h.reset();
    }

    test("Supersede file", [&]() {
        h = create_file(dir + u"\\supersede", MAXIMUM_ALLOWED, 0, 0, FILE_SUPERSEDE,
                        0, FILE_SUPERSEDED);
    });

    if (h) {
        test("Check attributes", [&]() {
            auto fbi = query_basic_information(h.get());

            if (fbi.FileAttributes != FILE_ATTRIBUTE_ARCHIVE) {
                throw formatted_error("attributes were {:x}, expected FILE_ATTRIBUTE_ARCHIVE",
                                      fbi.FileAttributes);
            }
        });

        h.reset();
    }

    test("Supersede adding hidden flag", [&]() {
        create_file(dir + u"\\supersede", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_HIDDEN, 0,
                    FILE_SUPERSEDE, 0, FILE_SUPERSEDED);
    });

    test("Try superseding while clearing hidden flag", [&]() {
        exp_status([&]() {
            create_file(dir + u"\\supersede", MAXIMUM_ALLOWED, 0, 0, FILE_SUPERSEDE,
                        0, FILE_SUPERSEDED);
        }, STATUS_ACCESS_DENIED);
    });

    test("Supersede adding system flag", [&]() {
        create_file(dir + u"\\supersede", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, 0,
                    FILE_SUPERSEDE, 0, FILE_SUPERSEDED);
    });

    test("Try superseding while clearing system flag", [&]() {
        exp_status([&]() {
            create_file(dir + u"\\supersede", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_HIDDEN, 0,
                        FILE_SUPERSEDE, 0, FILE_SUPERSEDED);
        }, STATUS_ACCESS_DENIED);
    });

    test("Try creating directory by FILE_SUPERSEDE", [&]() {
        exp_status([&]() {
            create_file(dir + u"\\supersededir", MAXIMUM_ALLOWED, 0, 0, FILE_SUPERSEDE,
                        FILE_DIRECTORY_FILE, FILE_CREATED);
        }, STATUS_INVALID_PARAMETER);
    });

    test("Create file", [&]() {
        h = create_file(dir + u"\\supersede2", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE,
                        0, FILE_CREATED);
    });

    if (h) {
        h.reset();

        test("Supersede file with different case", [&]() {
            h = create_file(dir + u"\\SUPERSEDE2", MAXIMUM_ALLOWED, 0, 0, FILE_SUPERSEDE,
                            0, FILE_SUPERSEDED);
        });

        if (h) {
            test("Check name", [&]() {
                auto fn = query_file_name_information(h.get());

                static const u16string_view ends_with = u"\\supersede2";

                if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                    throw runtime_error("Name did not end with \"\\supersede2\".");
            });
        }
    }

    test("Try overwriting non-existent file", [&]() {
        exp_status([&]() {
            create_file(dir + u"\\nonsuch", MAXIMUM_ALLOWED, 0, 0, FILE_OVERWRITE,
                        0, FILE_OVERWRITTEN);
        }, STATUS_OBJECT_NAME_NOT_FOUND);
    });

    test("Create readonly file", [&]() {
        h = create_file(dir + u"\\overwritero", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_READONLY, 0, FILE_CREATE,
                        0, FILE_CREATED);
    });

    if (h) {
        h.reset();

        test("Try overwriting readonly file", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\overwritero", MAXIMUM_ALLOWED, 0, 0, FILE_OVERWRITE,
                            0, FILE_OVERWRITTEN);
            }, STATUS_ACCESS_DENIED);
        });
    }

    test("Create file", [&]() {
        h = create_file(dir + u"\\overwrite", MAXIMUM_ALLOWED, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Try overwriting open file", [&]() {
            create_file(dir + u"\\overwrite", MAXIMUM_ALLOWED, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_OVERWRITE, 0, FILE_OVERWRITTEN);
        });

        h.reset();

        test("Overwrite file", [&]() {
            h = create_file(dir + u"\\overwrite", MAXIMUM_ALLOWED, 0, 0, FILE_OVERWRITE,
                            0, FILE_OVERWRITTEN);
        });
    }

    if (h) {
        h.reset();

        test("Overwrite file adding readonly flag", [&]() {
            create_file(dir + u"\\overwrite", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_READONLY, 0, FILE_OVERWRITE,
                        0, FILE_OVERWRITTEN);
        });
    }

    test("Create file", [&]() {
        h = create_file(dir + u"\\overwrite2", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE,
                        0, FILE_CREATED);
    });

    if (h) {
        h.reset();

        test("Try overwriting file, changing to directory", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\overwrite2", MAXIMUM_ALLOWED, 0, 0, FILE_OVERWRITE,
                            FILE_DIRECTORY_FILE, FILE_OVERWRITTEN);
            }, STATUS_INVALID_PARAMETER);
        });

        test("Overwrite file adding hidden flag", [&]() {
            h = create_file(dir + u"\\overwrite2", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_HIDDEN, 0, FILE_OVERWRITE,
                            0, FILE_OVERWRITTEN);
        });
    }

    if (h) {
        h.reset();

        test("Try overwriting file clearing hidden flag", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\overwrite2", MAXIMUM_ALLOWED, 0, 0, FILE_OVERWRITE,
                            0, FILE_OVERWRITTEN);
            }, STATUS_ACCESS_DENIED);
        });
    }

    test("Overwrite file adding system flag", [&]() {
        h = create_file(dir + u"\\overwrite2", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, 0,
                        FILE_OVERWRITE, 0, FILE_OVERWRITTEN);
    });

    if (h) {
        h.reset();

        test("Try overwriting file clearing system flag", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\overwrite2", MAXIMUM_ALLOWED, FILE_ATTRIBUTE_HIDDEN, 0, FILE_OVERWRITE,
                            0, FILE_OVERWRITTEN);
            }, STATUS_ACCESS_DENIED);
        });
    }

    test("Create directory", [&]() {
        h = create_file(dir + u"\\overwritedir", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE,
                        FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h) {
        h.reset();

        test("Try overwriting directory", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\overwritedir", MAXIMUM_ALLOWED, 0, 0, FILE_OVERWRITE,
                            FILE_DIRECTORY_FILE, FILE_OVERWRITTEN);
            }, STATUS_INVALID_PARAMETER);
        });

        test("Try overwriting directory, changing to file", [&]() {
            exp_status([&]() {
                create_file(dir + u"\\overwritedir", MAXIMUM_ALLOWED, 0, 0, FILE_OVERWRITE,
                            FILE_NON_DIRECTORY_FILE, FILE_OVERWRITTEN);
            }, STATUS_FILE_IS_A_DIRECTORY);
        });
    }

    test("Create file", [&]() {
        h = create_file(dir + u"\\overwrite3", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE,
                        0, FILE_CREATED);
    });

    if (h) {
        h.reset();

        test("Overwrite file with different case", [&]() {
            h = create_file(dir + u"\\OVERWRITE3", MAXIMUM_ALLOWED, 0, 0, FILE_OVERWRITE,
                            0, FILE_OVERWRITTEN);
        });

        if (h) {
            test("Check name", [&]() {
                auto fn = query_file_name_information(h.get());

                static const u16string_view ends_with = u"\\overwrite3";

                if (fn.size() < ends_with.size() || fn.substr(fn.size() - ends_with.size()) != ends_with)
                    throw runtime_error("Name did not end with \"\\overwrite3\".");
            });
        }
    }

    // FIXME - FILE_OPEN_IF
    // FIXME - FILE_OVERWRITE_IF
    // FIXME - FILE_OPEN_BY_FILE_ID
    // FIXME - FILE_NO_INTERMEDIATE_BUFFERING
    // FIXME - check invalid names (invalid characters, > 255 UTF-16, > 255 UTF-8, invalid UTF-16)
    // FIXME - test all the variations of NtQueryInformationFile

    // FIXME - reading
    // FIXME - writing

    // FIXME - preallocation

    // FIXME - check with case-sensitive flag set

    // FIXME - reparse points (opening, opening following link, creating, setting, querying tag)

    // FIXME - ADSes (including prohibited names)

    // FIXME - EAs
    // FIXME - FILE_NO_EA_KNOWLEDGE

    // FIXME - renaming (check names before and after)
    // FIXME - moving
    // FIXME - renaming by overwrite (if different case, will be filename be old or new?)
    // FIXME - POSIX renames
    // FIXME - FILE_RENAME_IGNORE_READONLY_ATTRIBUTE
    // FIXME - check invalid names (invalid characters, > 255 UTF-16, > 255 UTF-8, invalid UTF-16)

    // FIXME - deletion (file, empty directory, non-empty directory, opening doomed file, commuting sentence)
    // FIXME - POSIX deletion
    // FIXME - FILE_DELETE_ON_CLOSE
    // FIXME - FILE_DISPOSITION_FORCE_IMAGE_SECTION_CHECK
    // FIXME - FILE_DISPOSITION_ON_CLOSE
    // FIXME - FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE

    // FIXME - hard links
    // FIXME - linking by overwrite
    // FIXME - POSIX hard links
    // FIXME - FILE_LINK_IGNORE_READONLY_ATTRIBUTE
    // FIXME - check invalid names (invalid characters, > 255 UTF-16, > 255 UTF-8, invalid UTF-16)

    // FIXME - setting file information

    // FIXME - querying SD
    // FIXME - setting SD
    // FIXME - inheriting SD
    // FIXME - open files asking for too many permissions
    // FIXME - MAXIMUM_ALLOWED

    // FIXME - querying directory (inc. specific files)
    // FIXME - directory notifications

    // FIXME - oplocks
    // FIXME - FILE_RESERVE_OPFILTER
    // FIXME - FILE_OPEN_REQUIRING_OPLOCK
    // FIXME - FILE_COMPLETE_IF_OPLOCKED

    // FIXME - IOCTLs and FSCTLs

    // FIXME - querying volume info
    // FIXME - setting volume label

    // FIXME - locking

    // FIXME - reflink copies
    // FIXME - creating subvols
    // FIXME - snapshots
    // FIXME - sending and receiving(?)
}

static u16string to_u16string(time_t n) {
    u16string s;

    while (n > 0) {
        s += (n % 10) + u'0';
        n /= 10;
    }

    return u16string(s.rbegin(), s.rend());
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        fmt::print(stderr, "Usage: test.exe dir\n");
        return 1;
    }

    u16string ntdir = u"\\??\\"s + u16string((char16_t*)argv[1]);
    ntdir += u"\\" + to_u16string(time(nullptr));

    create_file(ntdir, GENERIC_WRITE, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);

    // FIXME - can we print name and version of FS driver?

    test_create_file(ntdir);

    return 0;
}