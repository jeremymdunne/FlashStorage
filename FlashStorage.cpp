/**
 * @file FlashStorage.cpp
 * @author Jeremy Dunne 
 * @brief Implementation of the FlashStorage library 
 * @version 0.1
 * @date 2022-12-23
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "FlashStorage.hpp"

FlashStorage_status_t FlashStorage::init(int cs_pin){
    // initialize the W25Q64 
    _flash_status = _flash.init(cs_pin); 
    if(_flash_status != W25Q64_OK){
        // assume a complete failure for now 
        return FLASH_STORAGE_FLASH_FAIL; 
    }
    // check for a FAT table 
    _status = readFAT();
    // report that status 
    return _status; 
}

FlashStorage_status_t FlashStorage::initializeFAT(){
    // create a new FAT table 
    // can also be used to erase a previous FAT 
    // allow this to be blocking 
    while(_flash.busy()); 
    _fat.file_count = 0; 
    return writeFAT();
}

FlashStorage_status_t FlashStorage::getFAT(FlashStorageFAT* fat){
    // do a memcpy 
    memcpy(fat, &_fat, sizeof(_fat)); 
    return FLASH_STORAGE_OK; 
}

FlashStorage_status_t FlashStorage::newFile(){
    // check and close if a file is open 
    close(); 
    // add a new file to the _fat table 
    if(_fat.file_count + 1 < FLASH_STORAGE_MAX_FILE_NUMBER){
        // determine the new start address 
        unsigned long new_addr = 1<<12; 
        if(_fat.file_count != 0){
            new_addr = ((_fat.files[ _fat.file_count-1].end_addr >> 12) + 1) << 12; // new sector  
        }
        // add the new file to the FAT 
        _fat.file_count ++;
        _fat.files[_fat.file_count-1].start_addr = new_addr; 
        _fat.files[_fat.file_count-1].end_addr =  new_addr; 
        // set the opened file indicator 
        _opened_file = _fat.file_count; 
        // set the mode 
        _mode = FLASH_STORAGE_WRITE_MODE; 
        // go ahead and start an erase at this location 
        while(_flash.busy()); 
        _flash.writeEnable(); 
        _flash.sectorErase(new_addr); 
        // update the pointers 
        _curr_addr = new_addr; 
        _max_erased_addr = _curr_addr + 4096; 
        // wait and write this  
        while(_flash.busy()); 
        return writeFAT(); 
    }
    else{
        return FLASH_STORAGE_NO_SPACE; 
    }
}

FlashStorage_status_t FlashStorage::openFile(unsigned int file_index){
    // check and close if a file is open 
    close(); 
    // check that the file index is valid 
    if(file_index > _fat.file_count){
        return FLASH_STORAGE_INVALID_FILE; 
    }
    // go ahead and update pointers 
    _opened_file = file_index; 
    _curr_addr = _fat.files[_opened_file-1].start_addr; 
    _mode = FLASH_STORAGE_READ_MODE; 
    // wait until free 
    while(_flash.busy()); 
    return FLASH_STORAGE_OK; 
}

FlashStorage_status_t FlashStorage::close(){
    // check the mode 
    if(_mode == FLASH_STORAGE_NO_MODE){
        return FLASH_STORAGE_OK; 
    }
    else if(_mode == FLASH_STORAGE_WRITE_MODE){
        // close out the writing file 
        // force a write of the buffer 
        writeFIFO(); 
        // update the FAT table 
        _fat.files[_opened_file-1].end_addr = _curr_addr; 
        // write the FAT table
        _opened_file = 0; 
        while(_flash.busy()); 
        writeFAT();  
        // update 
        _curr_addr = 0; 
        _max_erased_addr = 0; 
        _mode == FLASH_STORAGE_NO_MODE; 
    }
    else if(_mode == FLASH_STORAGE_READ_MODE){
        // just remove the indexes 
        _opened_file = 0; 
        _curr_addr = 0; 
        _max_erased_addr = 0; 
        _mode == FLASH_STORAGE_NO_MODE; 
    }
    return FLASH_STORAGE_OK; 
}

FlashStorage_status_t FlashStorage::write(byte* buff, unsigned int length){
    // check mode 
    if(_mode != FLASH_STORAGE_WRITE_MODE) return FLASH_STORAGE_WRONG_MODE; 
    // copy the data into the fifo buffer and write if needed 
    unsigned int remaining = FLASH_STORAGE_FIFO_BUFFER_SIZE - _buff_index; 
    if(remaining > length){
        // preform a straight copy 
        memcpy(&_buff[_buff_index], buff, length); 
        _buff_index += length; 
    }
    else{ 
        // otherwise fill up the buffer and trigger a write 
        unsigned int index = remaining; 
        memcpy(&_buff[_buff_index], buff, remaining); 
        _buff_index += remaining; 
        writeFIFO(); 
        // fill up the FIFO and keep writing if necessary 
        while(index + FLASH_STORAGE_FIFO_BUFFER_SIZE < length){ 
            memcpy(_buff, &buff[index], FLASH_STORAGE_FIFO_BUFFER_SIZE); 
            index += FLASH_STORAGE_FIFO_BUFFER_SIZE; 
            _buff_index = FLASH_STORAGE_FIFO_BUFFER_SIZE; 
            writeFIFO(); 
            // Serial.println("Looping write"); 
        }
        // fill out the remaining data 
        memcpy(_buff, &buff[index], length - index); 
        _buff_index += length - index; 
    } 
    // check that we're not exceeding the look ahead 
    if(_curr_addr > _max_erased_addr - _lookahead_erase_size){
        // trigger a sector erase 
        return eraseNextSector(); 
    }
    return FLASH_STORAGE_OK; 
} 

unsigned int FlashStorage::read(byte* buff, unsigned int length){
    // check the mode 
    if(_mode != FLASH_STORAGE_READ_MODE) return 0; 
    //Serial.print("Curr Addr: "); 
    //Serial.println(_curr_addr);
    //Serial.print("End Addr: "); 
    //Serial.println(_fat.files[_opened_file-1].end_addr); 
    // read up to the requested amount 
    if(length > _fat.files[_opened_file-1].end_addr - _curr_addr) length = _fat.files[_opened_file-1].end_addr - _curr_addr; 
    //_flash_status = _flash.readData(_curr_addr, buff, length); 
    // perform a fast read 
    _flash_status = _flash.fastRead(_curr_addr, buff, length); 
    if(_flash_status != W25Q64_OK){
        Serial.print("Flash Status Code: "); 
        Serial.println(_flash_status); 
        return 0; 
    } 
    _curr_addr += length; 
    return length; 
}

unsigned int FlashStorage::peek(){
    // check the mode 
    if(_mode != FLASH_STORAGE_READ_MODE) return 0; 
    return _fat.files[_opened_file-1].end_addr - _curr_addr; 
}

FlashStorage_status_t FlashStorage::deleteLastFile(){
    // remove the last file from the FAT table 
    // make sure no mode 
    if(_mode != FLASH_STORAGE_NO_MODE) return FLASH_STORAGE_WRONG_MODE; 
    if(_fat.file_count > 0) _fat.file_count --; 
    // write the fat 
    while(_flash.busy()); 
    writeFAT(); 
    return FLASH_STORAGE_OK; 
}

FlashStorage_status_t FlashStorage::deleteAllFiles(){
    // remove the last file from the FAT table 
    // make sure no mode 
    if(_mode != FLASH_STORAGE_NO_MODE) return FLASH_STORAGE_WRONG_MODE; 
    _fat.file_count = 0; 
    // write the fat 
    while(_flash.busy()); 
    writeFAT(); 
    return FLASH_STORAGE_OK; 
}

FlashStorage_status_t FlashStorage::writeFIFO(){
    // write the entire FIFO buffer 
    // check that the max erased address won't be exceeded 
    if(_curr_addr + _buff_index >= _max_erased_addr){
        while(_flash.busy()); 
        _flash.writeEnable(); 
        _flash.sectorErase(_max_erased_addr); 
        _max_erased_addr += 4096; 
    }
    // must be performed in 256 byte chunks 
    // check that we are not in the middle of a page 
    unsigned int offset = 0; 
    if(_curr_addr%256 != 0){
        // need to fill out the last part of the current page 
        unsigned int remaining = (_curr_addr>>8+1<<8) - _curr_addr; 
         // wait until free 
        while(_flash.busy()); 
        // enable write 
        _flash.writeEnable(); 
        _flash.pageProgram(_curr_addr,_buff, remaining); 
        _curr_addr += remaining; 
        offset = remaining; 
    }
    for(int p = 0; p < (_buff_index-offset)/256 + 1; p ++){
        // wait until free 
        while(_flash.busy()); 
        // enable write 
        _flash.writeEnable(); 
        // check the size, can only write up to a page 
        int size = (_buff_index-offset) - p*256;
        if(size < 0) size += 256; 
        if(size > 256) size = 256; 
        _flash.pageProgram(_curr_addr, &_buff[p*256+offset], size); 
        //Serial.println("FIFO"); 
        //Serial.println(size); 
        //Serial.println(offset); 
        //Serial.println(p); 
        _curr_addr += size; 
    }
    // update the FAT data
    // _fat.files[_opened_file-1].end_addr = _curr_addr; 
    // clear out the fifo 
    _buff_index = 0; 
    // trigger a new erase if needed 
    //Serial.print("END FIFO"); 
    //Serial.println(_curr_addr); 

    return FLASH_STORAGE_OK; 
}

FlashStorage_status_t FlashStorage::readFAT(){
    // read for the fat table 
    char id_string[] = FLASH_STORAGE_IDENTIFICATION_STRING;
    unsigned int read_size = sizeof(id_string)/sizeof(char); 
    byte buff[read_size]; 
    _flash_status = _flash.readData(0, buff, read_size);  
    if(_flash_status != W25Q64_OK){
        // check that its not a busy 
        if(_flash_status == W25Q64_BUSY) return FLASH_STORAGE_BUSY; 
        return FLASH_STORAGE_FLASH_FAIL; 
    }
    // compare 
    if(strcmp(id_string, (char*)buff) == 0){
        // id matches, read for data 
        // first get the file length 
        byte header[2]; 
        _flash_status = _flash.readData(read_size, header, 2); 
        // extrapolate the FAT size 
        // FAT size is file_count * (2 bytes for start page + 2 bytes for end page + 1 byte for page offset)
        unsigned int fat_len = header[0]*5; 
        byte fat_contents[fat_len]; 
        _flash_status = _flash.readData(read_size + 2, fat_contents, fat_len); 
        // construct the FAT 
        for(int i = 0; i < header[0]; i ++){
            _fat.files[i].start_addr = (fat_contents[i*5] << 8 | fat_contents[i*5+1])<<8; 
            _fat.files[i].end_addr = (fat_contents[i*5+2] << 8 | fat_contents[i*5+3])<<8 + fat_contents[i*5+4]; 
        }
        // set the file count 
        _fat.file_count = header[0]; 
        // TODO implement the potential data corruption check 
        // return success 
        return FLASH_STORAGE_OK;   
    }
    _fat.file_count = 0; 
    // no FAT table found 
    // report as such 
    return FLASH_STORAGE_NO_FAT_FOUND; 
}

FlashStorage_status_t FlashStorage::writeFAT(){
    // write the _fat table 
    // first request an erase 
    unsigned long start = millis(); 
    if(_flash.busy()) return FLASH_STORAGE_BUSY; 
    _flash.writeEnable(); 
    _flash.sectorErase(0); 
    // get the buffer length 
    char id_string[] = FLASH_STORAGE_IDENTIFICATION_STRING;
    unsigned int id_size = sizeof(id_string)/sizeof(char); 
    unsigned int header_size = id_size + 2; // this is fixed for now 
    unsigned int fat_size = header_size + _fat.file_count * 5; 
    byte buff[fat_size];  
    strcpy((char*)buff, id_string); 
    buff[id_size] = _fat.file_count; 
    buff[id_size + 1] = _opened_file; 
    for(int i = 0; i < _fat.file_count; i ++){
        buff[header_size + i*5] = _fat.files[i].start_addr>>16; 
        buff[header_size + i*5 + 1] = _fat.files[i].start_addr>>8; 
        buff[header_size + i*5 + 2] = _fat.files[i].end_addr>>16; 
        buff[header_size + i*5 + 3] = _fat.files[i].end_addr>>8; 
        buff[header_size + i*5 + 4] = _fat.files[i].end_addr; 
        /*
        Serial.print("File "); 
        Serial.print(i + 1); 
        Serial.print(" End Addr: "); 
        Serial.println(_fat.files[i].end_addr); 
        */ 
    }
    // perform the write action 
    // wait until free now
    for(int p = 0; p < 1 + fat_size/256; p ++){ 
        // wait until free 
        while(_flash.busy()); 
        // enable write 
        _flash.writeEnable(); 
        // check the size, can only write up to a page 
        int size = fat_size - p*256;
        if(size < 0) size += 256; 
        if(size > 256) size = 256; 
        _flash.pageProgram(0, &buff[p*256], size); 
        // Serial.println(size); 
    } 

    unsigned long end = millis(); 
    //Serial.print("Write FAT took: ");
    //Serial.print(end-start); 
    //Serial.println(" ms"); 
    return FLASH_STORAGE_OK; 
}

FlashStorage_status_t FlashStorage::eraseNextSector(){
    // erase the next sector 
    // check if busy 
    if(_flash.busy()) return FLASH_STORAGE_BUSY; 
    // erase at next place 
    _flash.writeEnable(); 
    _flash.sectorErase(_max_erased_addr); 
    _max_erased_addr += 4096; 
    return FLASH_STORAGE_OK; 
}