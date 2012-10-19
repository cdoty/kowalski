/*
Copyright (c) 2010-2012 Per Gantelius

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/

#ifndef KWL_XML_VALIDATION_H
#define KWL_XML_VALIDATION_H

#include "kwl_logging.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    typedef enum
    {
        KWL_DATA_IS_VALID = 0,
        KWL_XML_PARSING_ERROR,
        KWL_XSD_PARSING_ERROR,
        KWL_PROJECT_STRUCTURE_ERROR,
        KWL_INVALID_FILE_IDENTIFIER
    } kwlDataValidationResult;
    
    /**
     *
     */
    kwlDataValidationResult kwlValidateProjectData(const char* xmlPath, const char* xsdPath, kwlLogCallback logCallback);
    
    /**
     *
     */
    kwlDataValidationResult kwlValidateWaveBank(const char* waveBankPath, kwlLogCallback logCallback);
    
    /**
     *
     */
    kwlDataValidationResult kwlValidateEngineData(const char* engineDataPath, kwlLogCallback logCallback);

    
#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /*KWL_XML_VALIDATION_H*/