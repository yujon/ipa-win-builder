//
//  Archiver.hpp
//  AltSign-Windows
//
//

#ifndef Archiver_hpp
#define Archiver_hpp

#include <string>

void UnzipArchive(std::string archivePath, std::string outputDirectory);

std::string UnzipAppBundle(std::string filepath, std::string outputDirectory);
std::string ZipAppBundle(std::string filepath);


#endif /* Archiver_hpp */
