/**
 * @file FlashStorage.hpp
 * @author Jeremy Dunne 
 * @brief Flash Storage header file 
 * @version 0.1
 * @date 2022-12-23
 * 
 * This is a library to enable writing and recall of data on a Flash Chip. Implements a FAT-like approach to organize and access data. 
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef _FLASH_STORAGE_HPP_
#define _FLASH_STORAGE_HPP_

// includes 
#include <Arduino.h> 
#include "./lib/W25Q64/W25Q64.hpp"

// pre-definitions
#define FLASH_STORAGE_IDENTIFICATION_STRING "FLASH"
#define FLASH_STORAGE_FIFO_BUFFER_SIZE 1024 
#define FLASH_STORAGE_MAX_FILE_NUMBER 32  
#define FLASH_STORAGE_STANDARD_LOOKAHEAD_SIZE 1024 


typedef enum{
    FLASH_STORAGE_OK = 0, 
    FLASH_STORAGE_FLASH_FAIL,
    FLASH_STORAGE_BUSY, 
    FLASH_STORAGE_NO_FAT_FOUND, 
    FLASH_STORAGE_NO_SPACE,
    FLASH_STORAGE_INVALID_FILE,
    FLASH_STORAGE_WRONG_MODE  
} FlashStorage_status_t; 

struct FlashStorageFile{
    unsigned long start_addr; 
    unsigned long end_addr; 
}; 


/*
    FAT table implementation notes: 
        FAT table is always found at the 0x00 addr 
        FAT table will start with the FLASH_STORAGE_IDENTIFICATION_STRING. This is used to determine if there is actually a FAT table on the chip or not 
        The next byte is the file count (1 indexed) 
        The next byte is the in-progress file index (1 indexed). This is used to determine if a file was not properly closed out previously. This will be  
            255 if no file was opened at the end of previous runtime. 
        The remaining data is, in order of the file: 
            2 bytes for the start page address
            2 bytes for the page length 
            1 byte for the page offset (the last written index). A 0 represents no data written to the last page (i.e. file contents ended on the previous page + 255 offset). A 255

*/
struct FlashStorageFAT{
    FlashStorageFile files[FLASH_STORAGE_MAX_FILE_NUMBER]; 
    unsigned int file_count; 
}; 

typedef enum{
    FLASH_STORAGE_NO_MODE = 0, 
    FLASH_STORAGE_READ_MODE,
    FLASH_STORAGE_WRITE_MODE  
} FlashStorageMode; 

class FlashStorage{
public: 

    /**
     * @brief initialize the FlashStorage class 
     * 
     * Initializes the Flash Chip, checks for a FAT table. 
     * 
     * @param cs_pin chip select pin for the Flash Chip 
     * @return FlashStorage_status_t 
     */
    FlashStorage_status_t init(int cs_pin); 

    /**
     * @brief initializes a blank FAT table on the flash chip 
     * 
     * Removes any reference to data on the chip by clearing the FAT tabl. 
     * 
     * @return FlashStorage_status_t 
     */
    FlashStorage_status_t initializeFAT(); 

    /**
     * @brief get a copy of the FAT table from the chip 
     * 
     * Copies over _fat to the provided table 
     * 
     * @param fat pointer to the FAT table to copy into 
     * @return FlashStorage_status_t 
     */
    FlashStorage_status_t getFAT(FlashStorageFAT* fat); 

    /**
     * @brief opens a new file for writing 
     * 
     * Opens and records a new file in the FAT table. Erases the first sector to get ready for writing. 
     * 
     * @return FlashStorage_status_t 
     */
    FlashStorage_status_t newFile(); 

    /**
     * @brief opens a file for reading (not intended for appending for now) 
     * 
     * @return FlashStorage_status_t 
     */
    FlashStorage_status_t openFile(unsigned int file_index);

    /**
     * @brief close out the current file
     * 
     * Handles closing both files being written to and files being read from 
     * 
     * @return FlashStorage_status_t 
     */
    FlashStorage_status_t close(); 

    /**
     * @brief write data to the opened file 
     * 
     * Writes to the FIFO buffer and handles writing when necessary 
     * 
     * @param buff buffer of data to write 
     * @param length length of data to write 
     * @return FlashStorage_status_t 
     */
    FlashStorage_status_t write(byte* buff, unsigned int length);

    /**
     * @brief read data from the opened file 
     * 
     * @param buff buffer to read into 
     * @param length length of data to read 
     * @return unsigned int number of bytes read 
     */
    unsigned int read(byte* buff, unsigned int length);   

    /**
     * @brief get the remaining length of the file 
     * 
     * @return unsigned int remaining length of the file (bytes) 
     */
    unsigned int peek(); 

    FlashStorage_status_t deleteLastFile(); 

    FlashStorage_status_t deleteAllFiles(); 

private: 
    byte _buff[FLASH_STORAGE_FIFO_BUFFER_SIZE]; 
    unsigned int _buff_index = 0;

    unsigned int _opened_file = 0; // 1 indexed! 
    unsigned long _curr_addr; // address to write to 
    unsigned long _max_erased_addr; // exclusive, should always be a multiple of 4096 (sector erase size) 
    unsigned long _lookahead_erase_size = FLASH_STORAGE_STANDARD_LOOKAHEAD_SIZE; // the size ahead to trigger an erase 

    W25Q64 _flash; 
    W25Q64_status_t _flash_status; 
    FlashStorageFAT _fat; 
    FlashStorage_status_t _status; 
    FlashStorageMode _mode; 

    /**
     * @brief writes the FIFO buffer contents 
     * 
     * Writes all data in the FIFO buffer. Intended to be used when closing out a file, or when the buffer is completely full. 
     * 
     * Does some extra things to make sure all writes are in 256 byte chunks. 
     * 
     * @return FlashStorage_status_t 
     */
    FlashStorage_status_t writeFIFO(); 

    /**
     * @brief reads and parses the FAT table (if any) 
     * 
     * Reads and parses the contents of the FAT table to _fat 
     * 
     * @return FlashStorage_status_t 
     */
    FlashStorage_status_t readFAT(); 

    /**
     * @brief write the FAT table to the chip 
     * 
     * Writes the FAT table found in _fat to the chip. Is blocking after beginning to write.  
     * 
     * @return FlashStorage_status_t 
     */
    FlashStorage_status_t writeFAT(); 


    FlashStorage_status_t eraseNextSector(); 

}; 


#endif
