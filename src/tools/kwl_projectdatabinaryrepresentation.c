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

#include <stdio.h>


#include "kwl_assert.h"
#include "kwl_binarybuilding.h"
#include "kwl_datavalidation.h"
#include "kwl_inputstream.h"
#include "kwl_memory.h"
#include "kwl_projectdatabinaryrepresentation.h"
#include "kwl_sound.h"
#include "kwl_xmlutil.h"

#define KWL_TEMP_STRING_LENGTH 1024







/**
 * returns the path up to the top group
 */
static const xmlChar* kwlGetNodePath(xmlNode* currentNode)
{
    xmlNode* path[KWL_TEMP_STRING_LENGTH];
    kwlMemset(path, 0, KWL_TEMP_STRING_LENGTH * sizeof(xmlNode*));
    
    xmlNode* n = currentNode;
    int idx = 0;
    while (!xmlStrEqual(n->parent->name, (xmlChar*)KWL_XML_PROJECT_NODE_NAME))
    {
        path[idx] = n;
        idx++;
        KWL_ASSERT(idx < KWL_TEMP_STRING_LENGTH);
        n = n->parent;
    }
    
    int numChars = 0;
    xmlChar tempStr[KWL_TEMP_STRING_LENGTH];
    kwlMemset(tempStr, 0, sizeof(xmlChar) * KWL_TEMP_STRING_LENGTH);
    for (int i = idx - 1; i >= 0; i--)
    {
        //id gets freed along with the xml structure
        xmlChar* id = kwlGetAttributeValue(path[i], "id");
        tempStr[numChars] = '/';
        kwlMemcpy(&tempStr[numChars + 1], id, xmlStrlen(id));
        numChars += xmlStrlen(id) + 1;
        KWL_ASSERT(numChars < KWL_TEMP_STRING_LENGTH);
    }
    tempStr[numChars] = '\0';
    
    xmlChar* pathStr = KWL_MALLOCANDZERO(numChars, "path string");
    kwlMemcpy(pathStr, &tempStr[1], numChars); //remove leading slash
    pathStr[numChars - 1] = '\0';
    return pathStr;
}

static void kwlTraverseNodeTree(xmlNode* root,
                         const char* branchNodeName,
                         const char* leafNodeName,
                         kwlNodeTraversalCallback callback,
                         void* userData)
{
    for (xmlNode* curr = root->children; curr != NULL; curr = curr->next)
    {
        if (xmlStrEqual(curr->name, (xmlChar*)branchNodeName))
        {
            kwlTraverseNodeTree(curr, branchNodeName, leafNodeName, callback, userData);
        }
        if (xmlStrEqual(curr->name, (xmlChar*)leafNodeName))
        {
            callback(curr, userData);
        }
    }
}

/**
 * Gather mix buses excluding sub buses
 */
static void kwlGatherMixBusesCallback(xmlNode* currentNode, void* b)
{
    kwlProjectDataBinaryRepresentation* bin = (kwlProjectDataBinaryRepresentation*)b;
    bin->mixBusChunk.numMixBuses += 1;
    bin->mixBusChunk.mixBuses = realloc(bin->mixBusChunk.mixBuses,
                                        sizeof(kwlMixBusChunk) * bin->mixBusChunk.numMixBuses);
    kwlMixBusChunk* c = &bin->mixBusChunk.mixBuses[bin->mixBusChunk.numMixBuses - 1];
    c->id = kwlGetAttributeValueCopy(currentNode, "id");
    KWL_ASSERT(c->id != NULL);
    c->numSubBuses = kwlGetChildCount(currentNode, KWL_XML_MIX_BUS_NAME);
    if (c->numSubBuses > 0)
    {
        c->subBusIndices = KWL_MALLOCANDZERO(c->numSubBuses * sizeof(int), "xml 2 bin sub buses");
    }
}

/**
 * gather sub buses of already gathered mix buses
 */
static void kwlGatherSubBusesCallback(xmlNode* node, void* b)
{
    kwlProjectDataBinaryRepresentation* bin = (kwlProjectDataBinaryRepresentation*)b;
    
    //find the mix bus to attach the children to
    kwlMixBusChunk* mb = NULL;
    for (int i = 0; i < bin->mixBusChunk.numMixBuses; i++)
    {
        kwlMixBusChunk* mbi = &bin->mixBusChunk.mixBuses[i];
        if (strcmp(mbi->id, kwlGetAttributeValue(node, KWL_XML_ATTR_MIX_BUS_ID)) == 0)
        {
            mb = mbi;
            break;
        }
    }
    KWL_ASSERT(mb != NULL);
    
    int childIdx = 0;
    for (xmlNode* curr = node->children; curr != NULL; curr = curr->next)
    {
        if (!xmlStrEqual(curr->name, (xmlChar*)KWL_XML_MIX_BUS_NAME))
        {
            continue;
        }
        
        //find the index of this sub bus
        const xmlChar* id = kwlGetAttributeValue(curr, "id");
        KWL_ASSERT(id != NULL);
        int idx = -1;
        for (int i = 0; i < bin->mixBusChunk.numMixBuses; i++)
        {
            if (strcmp(bin->mixBusChunk.mixBuses[i].id, id) == 0)
            {
                idx = i;
                break;
            }
        }
        
        mb->subBusIndices[childIdx] = idx;
        
        
        KWL_ASSERT(idx >= 0);
        childIdx++;
    }
}

static void kwlMixBusRootXMLToBin(xmlNode* projectRoot, kwlProjectDataBinaryRepresentation* bin)
{
    bin->mixBusChunk.chunkId = KWL_MIX_BUSES_CHUNK_ID;
    
    kwlTraverseNodeTree(projectRoot,
                        KWL_XML_MIX_BUS_NAME,
                        KWL_XML_MIX_BUS_NAME,
                        kwlGatherMixBusesCallback,
                        bin);
    
    kwlTraverseNodeTree(projectRoot,
                        KWL_XML_MIX_BUS_NAME,
                        KWL_XML_MIX_BUS_NAME,
                        kwlGatherSubBusesCallback,
                        bin);
}

static int kwlGetMixBusIndex(kwlProjectDataBinaryRepresentation* bin, const char* id)
{
    KWL_ASSERT(bin->mixBusChunk.numMixBuses > 0);
    
    for (int i = 0; i < bin->mixBusChunk.numMixBuses; i++)
    {
        kwlMixBusChunk* mbi = &bin->mixBusChunk.mixBuses[i];
        if (strcmp(id, mbi->id) == 0)
        {
            return i;
        }
    }
    
    KWL_ASSERT(0);
    return -1;
}

/**
 * Gather mix presets
 */
static void kwlGatherMixPresetsCallback(xmlNode* node, void* b)
{
    kwlProjectDataBinaryRepresentation* bin = (kwlProjectDataBinaryRepresentation*)b;
    const int numMixBuses = bin->mixBusChunk.numMixBuses;
    KWL_ASSERT(numMixBuses > 0);
    
    bin->mixPresetChunk.numMixPresets += 1;
    bin->mixPresetChunk.mixPresets = realloc(bin->mixPresetChunk.mixPresets,
                                             sizeof(kwlMixPresetChunk) * bin->mixPresetChunk.numMixPresets);
    kwlMixPresetChunk* c = &bin->mixPresetChunk.mixPresets[bin->mixPresetChunk.numMixPresets - 1];
    c->id = kwlGetNodePath(node);
    c->isDefault = kwlGetIntAttributeValue(node, KWL_XML_ATTR_MIX_PRESET_IS_DEFAULT);
    c->mixBusIndices = KWL_MALLOCANDZERO(numMixBuses * sizeof(int), "xml 2 bin mix preset bus indices");
    c->gainLeft = KWL_MALLOCANDZERO(numMixBuses * sizeof(int), "xml 2 bin mix preset bus gain l");
    c->gainRight = KWL_MALLOCANDZERO(numMixBuses * sizeof(int), "xml 2 bin mix preset bus gain r");
    c->pitch = KWL_MALLOCANDZERO(numMixBuses * sizeof(int), "xml 2 bin mix preset bus pitch");
    
    int paramSetIdx = 0;
    for (xmlNode* curr = node->children; curr != NULL; curr = curr->next)
    {
        if (xmlStrEqual(curr->name, (xmlChar*)KWL_XML_PARAM_SET_NAME))
        {
            char* busId = kwlGetAttributeValue(curr, KWL_XML_ATTR_PARAM_SET_BUS);
            const int busIdx = kwlGetMixBusIndex(bin, busId);
            float gainLeft = kwlGetFloatAttributeValue(curr, KWL_XML_ATTR_PARAM_SET_GAIN_L);
            float gainRight = kwlGetFloatAttributeValue(curr, KWL_XML_ATTR_PARAM_SET_GAIN_R);
            float pitch = kwlGetFloatAttributeValue(curr, KWL_XML_ATTR_PARAM_SET_PITCH);
            
            KWL_ASSERT(paramSetIdx < numMixBuses);
            c->gainLeft[paramSetIdx] = gainLeft;
            c->gainLeft[paramSetIdx] = gainRight;
            c->pitch[paramSetIdx] = pitch;
            c->mixBusIndices[paramSetIdx] = busIdx;
            paramSetIdx++;
        }
    }
    
    KWL_ASSERT(paramSetIdx == numMixBuses);
}

static void kwlMixPresetRootXMLToBin(xmlNode* projectRoot, kwlProjectDataBinaryRepresentation* bin)
{
    bin->mixPresetChunk.chunkId = KWL_MIX_PRESETS_CHUNK_ID;
    
    kwlTraverseNodeTree(projectRoot,
                        KWL_XML_MIX_PRESET_GROUP_NAME,
                        KWL_XML_MIX_PRESET_NAME,
                        kwlGatherMixPresetsCallback,
                        bin);
}

static void kwlGatherWaveBanksCallback(xmlNode* node, void* b)
{
    kwlProjectDataBinaryRepresentation* bin = (kwlProjectDataBinaryRepresentation*)b;
    bin->waveBankChunk.numWaveBanks += 1;
    bin->waveBankChunk.waveBanks = realloc(bin->waveBankChunk.waveBanks,
                                           sizeof(kwlWaveBankChunk) * bin->waveBankChunk.numWaveBanks);
    kwlWaveBankChunk* c = &bin->waveBankChunk.waveBanks[bin->waveBankChunk.numWaveBanks - 1];
    const xmlChar* path = kwlGetNodePath(node);
    c->id = path;
    c->numAudioDataEntries = kwlGetChildCount(node, KWL_XML_AUDIO_DATA_ITEM_NAME);
    KWL_ASSERT(c->numAudioDataEntries > 0);
    c->audioDataEntries = KWL_MALLOCANDZERO(c->numAudioDataEntries * sizeof(char*), "xml 2 bin wb entries");
    
    int idx = 0;
    //printf("reading wavebank %s\n", c->id);
    for (xmlNode* curr = node->children; curr != NULL; curr = curr->next)
    {
        if (xmlStrEqual(curr->name, (xmlChar*)KWL_XML_AUDIO_DATA_ITEM_NAME))
        {
            
            char* itemPath = kwlGetAttributeValueCopy(curr, KWL_XML_ATTR_REL_PATH);
            c->audioDataEntries[idx] = itemPath;
            //printf("    %s\n", itemPath);
            idx++;
        }
    }
    
    KWL_ASSERT(path != NULL);
}

static void kwlWaveBankRootXMLToBin(xmlNode* projectRoot, kwlProjectDataBinaryRepresentation* bin)
{
    bin->waveBankChunk.chunkId = KWL_WAVE_BANKS_CHUNK_ID;
    
    /*collect wave banks and their ids and audio data entry counts*/
    kwlTraverseNodeTree(projectRoot,
                        KWL_XML_WAVE_BANK_GROUP_NAME,
                        KWL_XML_WAVE_BANK_NAME,
                        kwlGatherWaveBanksCallback,
                        bin);
    /*store the total number of audio data items*/
    int totalNumItems = 0;
    for (int i = 0; i < bin->waveBankChunk.numWaveBanks; i++)
    {
        kwlWaveBankChunk* wbi = &bin->waveBankChunk.waveBanks[i];
        totalNumItems += wbi->numAudioDataEntries;
    }
    bin->waveBankChunk.numAudioDataItemsTotal = totalNumItems;
}

static int kwlGetWaveBankIndex(kwlProjectDataBinaryRepresentation* bin, const char* id)
{
    KWL_ASSERT(bin->waveBankChunk.numWaveBanks > 0);
    
    for (int i = 0; i < bin->waveBankChunk.numWaveBanks; i++)
    {
        kwlWaveBankChunk* wbi = &bin->waveBankChunk.waveBanks[i];
        if (strcmp(id, wbi->id) == 0)
        {
            return i;
        }
    }
    
    KWL_ASSERT(0);
    return -1;
}


static int kwlGetAudioDataIndex(kwlWaveBankChunk* wb, const char* id)
{
    KWL_ASSERT(wb->numAudioDataEntries > 0);
    
    for (int i = 0; i < wb->numAudioDataEntries; i++)
    {
        
        if (strcmp(id, wb->audioDataEntries[i]) == 0)
        {
            return i;
        }
    }
    
    KWL_ASSERT(0);
    return -1;
}


static void kwlGatherSoundsCallback(xmlNode* node, void* b)
{
    kwlProjectDataBinaryRepresentation* bin = (kwlProjectDataBinaryRepresentation*)b;
    bin->soundChunk.numSoundDefinitions += 1;
    bin->soundChunk.soundDefinitions = realloc(bin->soundChunk.soundDefinitions,
                                               sizeof(kwlSoundChunk) * bin->soundChunk.numSoundDefinitions);
    kwlSoundChunk* c = &bin->soundChunk.soundDefinitions[bin->soundChunk.numSoundDefinitions - 1];
    
    c->gain = kwlGetFloatAttributeValue(node, KWL_XML_ATTR_SOUND_GAIN);
    c->gainVariation = kwlGetFloatAttributeValue(node, KWL_XML_ATTR_SOUND_GAIN_VAR);
    c->pitch = kwlGetFloatAttributeValue(node, KWL_XML_ATTR_PITCH);
    c->pitchVariation = kwlGetFloatAttributeValue(node, KWL_XML_ATTR_PITCH_VAR);
    c->deferStop = kwlGetIntAttributeValue(node, KWL_XML_ATTR_DEFER_STOP);
    c->playbackCount = kwlGetIntAttributeValue(node, KWL_XML_ATTR_PLAYBACK_COUNT);
    c->playbackMode = kwlGetIntAttributeValue(node, KWL_XML_ATTR_PLAYBACK_MODE);
    c->numWaveReferences = kwlGetChildCount(node, KWL_XML_AUDIO_DATA_REFERENCE_NAME);
    KWL_ASSERT(c->numWaveReferences > 0);
    
    if (c->numWaveReferences > 0)
    {
        c->waveBankIndices = KWL_MALLOCANDZERO(c->numWaveReferences * sizeof(int), "xml 2 bin sound wb indices");
        c->audioDataIndices = KWL_MALLOCANDZERO(c->numWaveReferences * sizeof(int), "xml 2 bin sound ad indices");
    }
    
    int refIdx = 0;
    for (xmlNode* curr = node->children; curr != NULL; curr = curr->next)
    {
        if (xmlStrEqual(curr->name, (xmlChar*)KWL_XML_AUDIO_DATA_REFERENCE_NAME))
        {
            xmlChar* filePath = kwlGetAttributeValue(curr, KWL_XML_ATTR_AUDIO_DATA_REFERENCE_PATH);
            xmlChar* waveBankId = kwlGetAttributeValue(curr, KWL_XML_ATTR_AUDIO_DATA_REFERENCE_WAVEBANK);
            
            const int wbIdx = kwlGetWaveBankIndex(bin, waveBankId);
            kwlWaveBankChunk* wb = &bin->waveBankChunk.waveBanks[wbIdx];
            const int itemIdx = kwlGetAudioDataIndex(wb, filePath);
            
            c->waveBankIndices[refIdx] = wbIdx;
            c->audioDataIndices[refIdx] = itemIdx;
            
            KWL_ASSERT(refIdx < bin->waveBankChunk.numAudioDataItemsTotal);
            refIdx++;
        }
    }
    
    KWL_ASSERT(refIdx == c->numWaveReferences);
}

static void kwlSoundRootXMLToBin(xmlNode* projectRoot, kwlProjectDataBinaryRepresentation* bin)
{
    bin->soundChunk.chunkId = KWL_SOUNDS_CHUNK_ID;
    
    kwlTraverseNodeTree(projectRoot,
                        KWL_XML_SOUND_GROUP_NAME,
                        KWL_XML_SOUND_NAME,
                        kwlGatherSoundsCallback,
                        bin);
}

static void kwlGatherEventsCallback(xmlNode* node, void* b)
{
    kwlProjectDataBinaryRepresentation* bin = (kwlProjectDataBinaryRepresentation*)b;
    bin->eventChunk.numEventDefinitions += 1;
    bin->eventChunk.eventDefinitions = realloc(bin->eventChunk.eventDefinitions,
                                               sizeof(kwlEventChunk) * bin->eventChunk.numEventDefinitions);
    kwlEventChunk* c = &bin->eventChunk.eventDefinitions[bin->eventChunk.numEventDefinitions - 1];
    c->id = kwlGetNodePath(node);
    
    c->outerConeAngleDeg = kwlGetFloatAttributeValue(node, KWL_XML_ATTR_EVENT_OUTER_ANGLE);
    c->innerConeAngleDeg = kwlGetFloatAttributeValue(node, KWL_XML_ATTR_EVENT_INNER_ANGLE);
    c->gain = kwlGetFloatAttributeValue(node, KWL_XML_ATTR_EVENT_GAIN);
    c->pitch = kwlGetFloatAttributeValue(node, KWL_XML_ATTR_EVENT_PITCH);
    c->instanceCount = kwlGetFloatAttributeValue(node, KWL_XML_ATTR_EVENT_INSTANCE_COUNT);
    c->isPositional = kwlGetBoolAttributeValue(node, KWL_XML_ATTR_EVENT_IS_POSITIONAL);
    c->mixBusIndex = kwlGetMixBusIndex(bin, kwlGetAttributeValue(node, KWL_XML_ATTR_EVENT_BUS));
    
    c->numReferencedWaveBanks = 0; //TODO*/
    //TODO
}

static void kwlEventRootXMLToBin(xmlNode* projectRoot, kwlProjectDataBinaryRepresentation* bin)
{
    bin->eventChunk.chunkId = KWL_EVENTS_CHUNK_ID;
    
    kwlTraverseNodeTree(projectRoot,
                        KWL_XML_EVENT_GROUP_NAME,
                        KWL_XML_EVENT_NAME,
                        kwlGatherEventsCallback,
                        bin);
}



void kwlProjectDataBinaryRepresentation_loadFromXML(kwlProjectDataBinaryRepresentation* bin,
                                                    const char* xmlPath,
                                                    const char* xsdPath,
                                                    kwlLogCallback errorLogCallback)

{
    xmlDoc *doc = kwlLoadAndValidateProjectDataDoc(xmlPath, xsdPath);
    
    /*Get the root element node */
    xmlNode* projectRootNode = xmlDocGetRootElement(doc);
    
    /*Process mix bus, mix presets, wave banks, sounds and events*/
    xmlNode* mixBusRootNode = NULL;
    xmlNode* mixPresetRootNode = NULL;
    xmlNode* waveBankRootNode = NULL;
    xmlNode* soundRootNode = NULL;
    xmlNode* eventRootNode = NULL;
    
    for (xmlNode* currNode = projectRootNode->children; currNode; currNode = currNode->next)
    {
        if (xmlStrcmp(currNode->name, (xmlChar*)KWL_XML_MIX_BUS_NAME) == 0)
        {
            mixBusRootNode = currNode;
        }
        else if (xmlStrcmp(currNode->name, (xmlChar*)KWL_XML_MIX_PRESET_GROUP_NAME) == 0)
        {
            mixPresetRootNode = currNode;
        }
        else if (xmlStrcmp(currNode->name, (xmlChar*)KWL_XML_WAVE_BANK_GROUP_NAME) == 0)
        {
            waveBankRootNode = currNode;
        }
        else if (xmlStrcmp(currNode->name, (xmlChar*)KWL_XML_SOUND_GROUP_NAME) == 0)
        {
            soundRootNode = currNode;
        }
        else if (xmlStrcmp(currNode->name, (xmlChar*)KWL_XML_EVENT_GROUP_NAME) == 0)
        {
            eventRootNode = currNode;
        }
    }
    
    KWL_ASSERT(projectRootNode != NULL);
    KWL_ASSERT(mixBusRootNode != NULL);
    KWL_ASSERT(mixPresetRootNode != NULL);
    KWL_ASSERT(waveBankRootNode != NULL);
    KWL_ASSERT(soundRootNode != NULL);
    KWL_ASSERT(eventRootNode != NULL);
    
    kwlMemset(bin, 0, sizeof(kwlProjectDataBinaryRepresentation));
    
    kwlMixBusRootXMLToBin(projectRootNode, bin);
    kwlMixPresetRootXMLToBin(mixPresetRootNode, bin);
    kwlWaveBankRootXMLToBin(waveBankRootNode, bin);
    kwlSoundRootXMLToBin(soundRootNode, bin);
    kwlEventRootXMLToBin(eventRootNode, bin);
    
    //kwlProjectDataBinaryRepresentation_dump(bin, kwlDefaultLogCallback);
    
    /*free the document */
    xmlFreeDoc(doc);
    
    /*
     *Free the global variables that may
     *have been allocated by the parser.
     */
    xmlCleanupParser();
}


void kwlProjectDataBinaryRepresentation_saveToBinary(kwlProjectDataBinaryRepresentation* bin,
                                                     const char* path)
{
    KWL_ASSERT(0 && "TODO");
}

void kwlProjectDataBinaryRepresentation_loadFromBinary(kwlProjectDataBinaryRepresentation* binaryRep,
                                                       const char* binaryPath,
                                                       kwlLogCallback errorLogCallbackIn)
{

    kwlMemset(binaryRep, 0, sizeof(kwlProjectDataBinaryRepresentation));
    
    kwlLogCallback errorLogCallback = errorLogCallbackIn == NULL ? kwlSilentLogCallback : errorLogCallbackIn;
    
    kwlInputStream is;
    kwlError result = kwlInputStream_initWithFile(&is, binaryPath);
    if (result != KWL_NO_ERROR)
    {
        //error loading file
        errorLogCallback("Could not open engine data binary %s\n", binaryPath);
        return;
    }
    
    /*check file identifier*/
    for (int i = 0; i < KWL_ENGINE_DATA_BINARY_FILE_IDENTIFIER_LENGTH; i++)
    {
        const char identifierChari = kwlInputStream_readChar(&is);
        binaryRep->fileIdentifier[i] = identifierChari;
        if (identifierChari != KWL_ENGINE_DATA_BINARY_FILE_IDENTIFIER[i])
        {
            //invalid file format
            kwlInputStream_close(&is);
            errorLogCallback("Invalid engine data binary file header\n");
            return;
        }
    }
    
    //mix buses
    {
        binaryRep->mixBusChunk.chunkId = KWL_MIX_BUSES_CHUNK_ID;
        binaryRep->mixBusChunk.chunkSize = kwlInputStream_seekToEngineDataChunk(&is, KWL_MIX_BUSES_CHUNK_ID);
        
        //allocate memory for the mix bus data
        binaryRep->mixBusChunk.numMixBuses = kwlInputStream_readIntBE(&is);
        binaryRep->mixBusChunk.mixBuses = KWL_MALLOCANDZERO(binaryRep->mixBusChunk.numMixBuses * sizeof(kwlMixBusChunk),
                                                           "bin mix buses");
        
        
        //read mix bus data
        for (int i = 0; i < binaryRep->mixBusChunk.numMixBuses; i++)
        {
            kwlMixBusChunk* mi = &binaryRep->mixBusChunk.mixBuses[i];
            mi->id = kwlInputStream_readASCIIString(&is);
            mi->numSubBuses = kwlInputStream_readIntBE(&is);
            mi->subBusIndices = NULL;
            
            if (mi->numSubBuses > 0)
            {
                mi->subBusIndices = KWL_MALLOCANDZERO(mi->numSubBuses * sizeof(int), "bin sub bus list");
                for (int j = 0; j < mi->numSubBuses; j++)
                {
                    const int subBusIndexj = kwlInputStream_readIntBE(&is);
                    if(subBusIndexj < 0 || subBusIndexj >= binaryRep->mixBusChunk.numMixBuses)
                    {
                        errorLogCallback("Mix bus %s sub bus index at %d is %d. Expected value in [0, %d]\n",
                                         mi->id, j, subBusIndexj, binaryRep->mixBusChunk.numMixBuses - 1);
                        goto onDataError;
                    }
                    mi->subBusIndices[j] = subBusIndexj;
                }
            }
        }
    }
    
    
    //mix presets
    {
        binaryRep->mixPresetChunk.chunkId = KWL_MIX_PRESETS_CHUNK_ID;
        binaryRep->mixPresetChunk.chunkSize = kwlInputStream_seekToEngineDataChunk(&is, KWL_MIX_PRESETS_CHUNK_ID);
        
        //allocate memory for the mix preset data
        binaryRep->mixPresetChunk.numMixPresets = kwlInputStream_readIntBE(&is);
        binaryRep->mixPresetChunk.mixPresets = KWL_MALLOCANDZERO(binaryRep->mixPresetChunk.numMixPresets * sizeof(kwlMixPresetChunk), "bin mix presets");
        
        const int numParameterSets = binaryRep->mixBusChunk.numMixBuses;
        int defaultPresetIndex = -1;
        
        //read data
        for (int i = 0; i < binaryRep->mixPresetChunk.numMixPresets; i++)
        {
            kwlMixPresetChunk* mpi = &binaryRep->mixPresetChunk.mixPresets[i];
            
            mpi->id = kwlInputStream_readASCIIString(&is);
            mpi->isDefault = kwlInputStream_readIntBE(&is);
            
            if (mpi->isDefault != 0)
            {
                if(defaultPresetIndex != -1)
                {
                    errorLogCallback("Multiple default mix presets found");
                    goto onDataError;
                }
                defaultPresetIndex = i;
            }
            
            mpi->gainLeft = (float*)KWL_MALLOCANDZERO(sizeof(float) * numParameterSets, "bin mp gains l");
            mpi->gainRight = (float*)KWL_MALLOCANDZERO(sizeof(float) * numParameterSets, "bin mp gains r");
            mpi->pitch = (float*)KWL_MALLOCANDZERO(sizeof(float) * numParameterSets, "bin mp pitches");
            mpi->mixBusIndices = (int*)KWL_MALLOCANDZERO(sizeof(int) * numParameterSets, "bin mp indices");
            
            for (int j = 0; j < numParameterSets; j++)
            {
                mpi->mixBusIndices[j] = kwlInputStream_readIntBE(&is);
                if (mpi->mixBusIndices[j] < 0 ||  mpi->mixBusIndices[j] >= numParameterSets)
                {
                    errorLogCallback("Mix preset %s references out of bounds index %d (mix bus count is %d)\n",
                                     mpi->id, mpi->mixBusIndices[j], binaryRep->mixBusChunk.numMixBuses);
                    goto onDataError;
                }
                mpi->gainLeft[j] = kwlInputStream_readFloatBE(&is);
                mpi->gainRight[j] = kwlInputStream_readFloatBE(&is);
                mpi->pitch[j] = kwlInputStream_readFloatBE(&is);
            }
        }
        
        if (defaultPresetIndex < 0)
        {
            errorLogCallback("No default mix preset found");
            goto onDataError;
        }
    }
    
    //wave bank data
    {
        binaryRep->waveBankChunk.chunkId = KWL_WAVE_BANKS_CHUNK_ID;
        binaryRep->waveBankChunk.chunkSize = kwlInputStream_seekToEngineDataChunk(&is, KWL_WAVE_BANKS_CHUNK_ID);
        
        /*deserialize wave bank structures*/
        binaryRep->waveBankChunk.numAudioDataItemsTotal = kwlInputStream_readIntBE(&is);
        if (binaryRep->waveBankChunk.numAudioDataItemsTotal < 0)
        {
            errorLogCallback("Expected at least one audio data entry, found %d", binaryRep->waveBankChunk.numAudioDataItemsTotal);
            goto onDataError;
        }
        
        binaryRep->waveBankChunk.numWaveBanks = kwlInputStream_readIntBE(&is);
        if (binaryRep->waveBankChunk.numWaveBanks < 0)
        {
            errorLogCallback("Expected at least one wave bank, found %d", binaryRep->waveBankChunk.numWaveBanks);
            goto onDataError;
        }
        
        binaryRep->waveBankChunk.waveBanks = KWL_MALLOCANDZERO(binaryRep->waveBankChunk.numWaveBanks * sizeof(kwlWaveBankChunk),
                                                              "bin wbs");
        
        int audioDataItemIdx = 0;
        for (int i = 0; i < binaryRep->waveBankChunk.numWaveBanks; i++)
        {
            kwlWaveBankChunk* wbi = &binaryRep->waveBankChunk.waveBanks[i];
            wbi->id = kwlInputStream_readASCIIString(&is);
            wbi->numAudioDataEntries = kwlInputStream_readIntBE(&is);
            if (wbi->numAudioDataEntries < 0)
            {
                errorLogCallback("Wave bank %s has %d audio data items. Expected at least 1.",
                                 wbi->id,
                                 wbi->numAudioDataEntries);
                goto onDataError;
            }
            
            wbi->audioDataEntries = KWL_MALLOCANDZERO(wbi->numAudioDataEntries * sizeof(char*), "bin wb audio data list");
            
            for (int j = 0; j < wbi->numAudioDataEntries; j++)
            {
                wbi->audioDataEntries[j] = kwlInputStream_readASCIIString(&is);
                audioDataItemIdx++;
            }
        }
    }
    
    //sound data
    {
        binaryRep->soundChunk.chunkId = KWL_SOUNDS_CHUNK_ID;
        binaryRep->soundChunk.chunkSize = kwlInputStream_seekToEngineDataChunk(&is, KWL_SOUNDS_CHUNK_ID);
        
        /*allocate memory for sound definitions*/
        binaryRep->soundChunk.numSoundDefinitions = kwlInputStream_readIntBE(&is);
        binaryRep->soundChunk.soundDefinitions = KWL_MALLOCANDZERO(binaryRep->soundChunk.numSoundDefinitions * sizeof(kwlSoundChunk),
                                                                  "bin sound defs");
        
        /*read sound definitions*/
        for (int i = 0; i < binaryRep->soundChunk.numSoundDefinitions; i++)
        {
            kwlSoundChunk* si = & binaryRep->soundChunk.soundDefinitions[i];
            si->playbackCount = kwlInputStream_readIntBE(&is);
            si->deferStop = kwlInputStream_readIntBE(&is);
            si->gain = kwlInputStream_readFloatBE(&is);
            si->gainVariation = kwlInputStream_readFloatBE(&is);
            si->pitch = kwlInputStream_readFloatBE(&is);
            si->pitchVariation = kwlInputStream_readFloatBE(&is);
            si->playbackMode = (kwlSoundPlaybackMode)kwlInputStream_readIntBE(&is);
            
            si->numWaveReferences = kwlInputStream_readIntBE(&is);
            if (si->numWaveReferences < 0)
            {
                errorLogCallback("Sound with index %d has %d audio data items. Expected at least 1.", i, si->numWaveReferences);
                goto onDataError;
            }
            
            si->waveBankIndices = KWL_MALLOCANDZERO(si->numWaveReferences * sizeof(int), "bin wb idcs");
            si->audioDataIndices = KWL_MALLOCANDZERO(si->numWaveReferences * sizeof(int), "bin ad idcs");
            
            for (int j = 0; j < si->numWaveReferences; j++)
            {
                si->waveBankIndices[j] = kwlInputStream_readIntBE(&is);
                si->audioDataIndices[j] = kwlInputStream_readIntBE(&is);
            }
        }
    }

    //event data
    {
        binaryRep->eventChunk.chunkId = KWL_EVENTS_CHUNK_ID;
        binaryRep->eventChunk.chunkSize = kwlInputStream_seekToEngineDataChunk(&is, KWL_EVENTS_CHUNK_ID);
        
        /*read the total number of event definitions*/
        binaryRep->eventChunk.numEventDefinitions = kwlInputStream_readIntBE(&is);
        if (binaryRep->eventChunk.numEventDefinitions <= 0)
        {
            errorLogCallback("Found %d event definitions. Expected at least 1.",
                             binaryRep->eventChunk.numEventDefinitions);
            goto onDataError;
        }
        
        binaryRep->eventChunk.eventDefinitions = KWL_MALLOCANDZERO(binaryRep->eventChunk.numEventDefinitions * sizeof(kwlEventChunk),
                                                                  "bin ev defs");

        for (int i = 0; i < binaryRep->eventChunk.numEventDefinitions; i++)
        {
            kwlEventChunk* ei = &binaryRep->eventChunk.eventDefinitions[i];
            
            /*read the id of this event definition*/
            ei->id = kwlInputStream_readASCIIString(&is);

            ei->instanceCount = kwlInputStream_readIntBE(&is);
            KWL_ASSERT(ei->instanceCount >= -1);

            ei->gain = kwlInputStream_readFloatBE(&is);
            ei->pitch = kwlInputStream_readFloatBE(&is);
            ei->innerConeAngleDeg = kwlInputStream_readFloatBE(&is);
            ei->outerConeAngleDeg = kwlInputStream_readFloatBE(&is);
            ei->outerConeGain = kwlInputStream_readFloatBE(&is);

            /*read the index of the mix bus that this event belongs to*/
            ei->mixBusIndex = kwlInputStream_readIntBE(&is);
            KWL_ASSERT(ei->mixBusIndex >= 0 && ei->mixBusIndex < binaryRep->mixBusChunk.numMixBuses);
            ei->isPositional = kwlInputStream_readIntBE(&is);

            /*read the index of the sound referenced by this event (ignored for streaming events)*/
            ei->soundIndex = kwlInputStream_readIntBE(&is);
            
            if (ei->soundIndex < -1)
            {
                errorLogCallback("Invalid sound index %d in event definition %s.", ei->soundIndex, ei->id);
                goto onDataError;
            }
            
            /*read the event retrigger mode (ignored for streaming events)*/
            ei->retriggerMode = (kwlEventRetriggerMode)kwlInputStream_readIntBE(&is);

            /*read the index of the audio data referenced by this event (only used for streaming events)*/
            ei->waveBankIndex = kwlInputStream_readIntBE(&is);
            ei->audioDataIndex = kwlInputStream_readIntBE(&is);
            
            /*read loop flag (ignored for non-streaming events)*/
            ei->loopIfStreaming = kwlInputStream_readIntBE(&is);

            /*read referenced wave banks*/
            ei->numReferencedWaveBanks = kwlInputStream_readIntBE(&is);
            ei->waveBankIndices = (int*)KWL_MALLOC(ei->numReferencedWaveBanks * sizeof(int),
                                                        "bin evt wave bank refs");

            for (int j = 0; j < ei->numReferencedWaveBanks; j++)
            {
                ei->waveBankIndices[j] = kwlInputStream_readIntBE(&is);
                if (ei->waveBankIndices[j] < 0 || ei->waveBankIndices[j] >= binaryRep->waveBankChunk.numWaveBanks)
                {
                    errorLogCallback("Invalid wave bank index index %d in event definition %s. Wave bank count is %d",
                                     ei->waveBankIndices[j],
                                     ei->id,
                                     binaryRep->waveBankChunk.numWaveBanks);
                    goto onDataError;
                }
            }
        }
    }
    
    kwlInputStream_close(&is);
    return;
    
    onDataError:
    kwlInputStream_close(&is);
    kwlProjectDataBinaryRepresentation_free(binaryRep);
    return;
}

void kwlProjectDataBinaryRepresentation_free(kwlProjectDataBinaryRepresentation* bin)
{
    if (bin->mixBusChunk.numMixBuses > 0)
    {
        
    }
    
    if (bin->mixPresetChunk.numMixPresets > 0)
    {
        
    }
    
    if (bin->waveBankChunk.numWaveBanks > 0)
    {
        
    }
    
    if (bin->soundChunk.numSoundDefinitions > 0)
    {
        
    }
    
    if (bin->eventChunk.numEventDefinitions > 0)
    {
        
    }
}

void kwlProjectDataBinaryRepresentation_dump(kwlProjectDataBinaryRepresentation* bin, kwlLogCallback logCallback)
{
    logCallback("Kowalski project data binary (");
    logCallback("file ID: ");
    for (int i = 0; i < KWL_ENGINE_DATA_BINARY_FILE_IDENTIFIER_LENGTH; i++)
    {
        logCallback("%d ", (int)bin->fileIdentifier[i]);
    }
    logCallback(")\n");
    
    logCallback("    Mix bus chunk (ID %d, %d bytes, %d mix buses):\n",
                bin->mixBusChunk.chunkId,
                bin->mixBusChunk.chunkSize,
                bin->mixBusChunk.numMixBuses);
    for (int i = 0; i < bin->mixBusChunk.numMixBuses; i++)
    {
        kwlMixBusChunk* mbi = &bin->mixBusChunk.mixBuses[i];
        logCallback("        %s (%d sub buses", mbi->id, mbi->numSubBuses);
        for (int j = 0; j < mbi->numSubBuses; j++)
        {
            logCallback("%s%d%s", j == 0 ? ": " : "", mbi->subBusIndices[j], j < mbi->numSubBuses - 1 ? ", " : "");
        }
        logCallback(")\n");
    }
    
    logCallback("\n");
    logCallback("    Mix preset chunk (ID %d, %d bytes, %d mix presets):\n",
                bin->mixPresetChunk.chunkId,
                bin->mixPresetChunk.chunkSize,
                bin->mixPresetChunk.numMixPresets);
    for (int i = 0; i < bin->mixPresetChunk.numMixPresets; i++)
    {
        kwlMixPresetChunk* mpi = &bin->mixPresetChunk.mixPresets[i];
        logCallback("        %s (default %d)\n", mpi->id, mpi->isDefault);
        for (int j = 0; j < bin->mixBusChunk.numMixBuses; j++)
        {
            logCallback("            bus idx %d: gain left %f, gain right %f, pitch %f\n",
                        mpi->mixBusIndices[j],
                        mpi->gainLeft[j],
                        mpi->gainRight[j],
                        mpi->pitch[j]);
        }
    }
    
    logCallback("\n");
    logCallback("    Wave bank chunk (ID %d, %d bytes, %d items total, %d wave banks):\n",
                bin->waveBankChunk.chunkId,
                bin->waveBankChunk.chunkSize,
                bin->waveBankChunk.numAudioDataItemsTotal,
                bin->waveBankChunk.numWaveBanks);
    for (int i = 0; i < bin->waveBankChunk.numWaveBanks; i++)
    {
        kwlWaveBankChunk* wbi = &bin->waveBankChunk.waveBanks[i];
        logCallback("        %s (%d entries)\n", wbi->id, wbi->numAudioDataEntries);
        for (int j = 0; j < wbi->numAudioDataEntries; j++)
        {
            logCallback("            %s\n", wbi->audioDataEntries[j]);
        }
    }
    
    logCallback("\n");
    logCallback("    Sound chunk (ID %d, %d bytes, %d sound definitions):\n",
                bin->soundChunk.chunkId,
                bin->soundChunk.chunkSize,
                bin->soundChunk.numSoundDefinitions);
    for (int i = 0; i < bin->soundChunk.numSoundDefinitions; i++)
    {
        kwlSoundChunk* si = &bin->soundChunk.soundDefinitions[i];
        logCallback("        %d%s: defer stop %d, playback count %d, num wave refs %d\n",
                    i, i < 10 ? " " : "", si->deferStop, si->playbackCount, si->numWaveReferences);
        logCallback("            gain (var) %f (%f), pitch (var) %f (%f)\n",
                    si->gain, si->gainVariation, si->pitch, si->pitchVariation);
        for (int j = 0; j < si->numWaveReferences; j++)
        {
            logCallback("                wave bank idx %d, item idx %d\n",
                        si->waveBankIndices[j],
                        si->audioDataIndices[j]);
        }
    }
    
    logCallback("\n");
    logCallback("    Event chunk (ID %d, %d bytes, %d event definitions):\n",
                bin->eventChunk.chunkId,
                bin->eventChunk.chunkSize,
                bin->eventChunk.numEventDefinitions);
    for (int i = 0; i < bin->eventChunk.numEventDefinitions; i++)
    {
        kwlEventChunk* ei = &bin->eventChunk.eventDefinitions[i];
        logCallback("        %s\n", ei->id);
    }
}
