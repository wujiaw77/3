#ifndef SUNSPECMODBUS_H
#define SUNSPECMODBUS_H

// INCLUDES
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <modbus/modbus-tcp.h>
#include "SunSpecModel.h"

// This class uses sunspec models to read/scale/transplate blocks of modbus
// registers to be used by a controller.
class SunSpecModbus {
public:
    SunSpecModbus (std::map <std::string, std::string>& configs);
    ~SunSpecModbus ();
    void ReadRegisters (unsigned int offset,
                        unsigned int length,
                        uint16_t *reg_ptr);
    void WriteRegisters (unsigned int offset,
                         unsigned int length,
                         std::vector <uint16_t> registers);

    std::map <std::string, std::string> ReadBlock (unsigned int did);

    void WriteBlock (
        unsigned int did, std::map <std::string, std::string>& points
    );
    void WritePoint (
        unsigned int did, std::map <std::string, std::string>& point
    );

    void PrintBlock (
        std::map <std::string, std::string>& block
    );

private:
    std::mutex mutex_;
    modbus_t* context_ptr_;
    std::string model_path_;
    unsigned int sunspec_key_;
    std::vector <std::shared_ptr <SunSpecModel>> models_;

private:
    std::string FormatModelPath (unsigned int did);
    void Query (unsigned int did);
};

#endif // SUNSPECMODBUS_H
