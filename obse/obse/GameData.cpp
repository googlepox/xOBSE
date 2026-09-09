#include "GameData.h"

#if OBLIVION
#include "GameAPI.h"
#else
#include "obse_editor\EditorAPI.h"
#endif

Archive** const g_archiveWhichProvidedLastFile = (Archive**) 0x00B338E4;
Archive** const g_firstLoadedArchivesByType    = (Archive**)0x00B338E8;
Archive** const g_ArchiveByTypeList    = (Archive**)0x00B3390C;

NiTArray<BSHash>* const g_archiveInvalidatedFilenames = (NiTArray<BSHash>*) 0x00B33930;
NiTArray<BSHash>* const g_archiveInvalidatedDirectoryPaths = (NiTArray<BSHash>*) 0x00B33934;

static const OBSE_ESLInterface* s_esl = nullptr;

extern "C" __declspec(dllexport)
void __cdecl OBSE_SetESLInterface(const OBSE_ESLInterface* iface)
{
	s_esl = iface;
}

const OBSE_ESLInterface* GetESLInterface()
{
	return s_esl;
}

std::unordered_map<std::string, UInt8>	s_modIndexCache;
UInt32									s_cachedCount = 0xFFFFFFFF;
ModEntry::Data* s_cachedFirst = nullptr;

std::string ToLower(const char* s)
{
	std::string out(s ? s : "");
	std::transform(out.begin(), out.end(), out.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	return out;
}

class LoadedModFinder
{
	const char * m_stringToFind;

public:
	LoadedModFinder(const char * str) : m_stringToFind(str) { }

	bool Accept(ModEntry::Data* data)
	{
		return _stricmp(data->name, m_stringToFind) == 0;
	}
};

const ModEntry * DataHandler::LookupModByName(const char * modName)
{
	return ModEntryVisitor(&modList).Find(LoadedModFinder(modName));
}

const ModEntry ** DataHandler::GetActiveModList()
{
	static const ModEntry* activeModList[0x100] = { 0 };
	static UInt32			cachedCount = 0xFFFFFFFF;
	static ModEntry::Data* cachedFirst = nullptr;

	ModEntry::Data* first = (numLoadedMods > 0) ? modsByID[0] : nullptr;

	if (numLoadedMods != cachedCount || first != cachedFirst)
	{
		memset(activeModList, 0, sizeof(activeModList));

		for (UInt32 idx = 0; idx < numLoadedMods && idx < 0x100; ++idx)
		{
			ModEntry::Data* data = modsByID[idx];

			if (data && data->name)
				activeModList[idx] = LookupModByName(data->name);
		}

		cachedCount = numLoadedMods;
		cachedFirst = first;
	}

	return activeModList;
}

void EnsureModIndexCache(DataHandler* dh)
{
	ModEntry::Data* first =
		(dh->numLoadedMods > 0) ? dh->modsByID[0] : nullptr;

	if (first != s_cachedFirst)
	{
		s_modIndexCache.clear();
		s_cachedCount = 0;
		s_cachedFirst = first;
	}

	if (dh->numLoadedMods <= s_cachedCount)
		return;

	for (UInt32 idx = s_cachedCount; idx < dh->numLoadedMods && idx < 0x100; ++idx)
	{
		ModEntry::Data* data = dh->modsByID[idx];

		if (data && data->name && data->name[0])
			s_modIndexCache[ToLower(data->name)] = (UInt8)idx;
	}

	s_cachedCount = dh->numLoadedMods;
}

// Returns the load order byte, or 0xFF if not found.
//
// ESLs are not in modsByID, so this returns 0xFF for them -- and that is
// correct. 0xFE identifies the container, not which of 4096 plugins, so
// returning it here would make callers build FormIDs pointing at ESL 0.
// Callers that need ESL support want GetFormIDBase.
UInt8 DataHandler::GetModIndex(const char* modName)
{
	if (!modName || !modName[0])
		return 0xFF;

	EnsureModIndexCache(this);

	auto it = s_modIndexCache.find(ToLower(modName));

	return (it != s_modIndexCache.end()) ? it->second : 0xFF;
}

UInt8 DataHandler::GetActiveModCount()
{
	return (UInt8)((numLoadedMods < 0xFF) ? numLoadedMods : 0xFF);
}

const char* DataHandler::GetNthModName(UInt32 modIndex)
{
	if (modIndex == 0xFE && s_esl)
		return "";

	if (modIndex >= numLoadedMods)
		return "";

	ModEntry::Data* data = modsByID[modIndex];

	return (data && data->name) ? data->name : "";
}

UInt32 DataHandler::GetFormIDBase(const char* modName)
{
	UInt8 index = GetModIndex(modName);

	if (index != 0xFF)
		return (UInt32)index << 24;

	if (s_esl && s_esl->GetFormIDBase)
		return s_esl->GetFormIDBase(modName);

	return kInvalidFormIDBase;
}

const char* DataHandler::GetModNameForFormID(UInt32 formID)
{
	UInt8 high = (formID >> 24) & 0xFF;

	if (high == 0xFF)
		return "";

	if (high == 0xFE && s_esl && s_esl->GetNameByIndex)
	{
		const char* name = s_esl->GetNameByIndex((UInt16)((formID >> 12) & 0x0FFF));

		return name ? name : "";
	}

	return GetNthModName(high);
}

TESGlobal* DataHandler::GetGlobalVarByName(const char* varName, UInt32 nameLen)
{
	if (nameLen == -1)
		nameLen = strlen(varName);

	for (tList<TESGlobal>::Iterator Itr = globals.Begin(); !Itr.End() && Itr.Get(); ++Itr)
	{
		TESGlobal* item = Itr.Get();

		if (item->name.m_dataLen == nameLen && !_stricmp(item->name.m_data, varName))
			return item;
	}

	return NULL;
}

TESQuest* DataHandler::GetQuestByEditorName(const char* questName, UInt32 nameLen)
{
	if (nameLen == -1)
		nameLen = strlen(questName);

	for (tList<TESQuest>::Iterator Itr = quests.Begin(); !Itr.End() && Itr.Get(); ++Itr)
	{
		TESQuest* quest = Itr.Get();
		if (quest->editorName.m_dataLen == nameLen && !_stricmp(quest->editorName.m_data, questName))
			return quest;
	}
	return NULL;
}

// runtime-only stuff
#if OBLIVION

bool DataHandler::ConstructObject(ModEntry::Data* tesFile, bool unk1)
{
	return ThisStdCall(0x0044DCF0, this, tesFile, unk1) ? true : false;
}

FileFinder** g_FileFinder = (FileFinder**)0xB33A04;
TimeGlobals* g_TimeGlobals = (TimeGlobals*)0x00B332E0;
UInt32* s_iHoursToRespawnCell = (UInt32*)0x00B35C1C;
UInt16* s_firstDayOfMonths = (UInt16*)0x00B06728;
UInt16* s_numDaysPerMonths = (UInt16*)0x00B06710;

TimeGlobals* TimeGlobals::Singleton()
{
	return g_TimeGlobals;
}

UInt32 TimeGlobals::GameHoursPassed()
{
	return GameDaysPassed() * 24 + GameHour();
}

UInt32 TimeGlobals::GameDay()
{
	return Singleton()->gameDay->data;
}

UInt32 TimeGlobals::GameYear()
{
	return Singleton()->gameYear->data;
}

UInt32 TimeGlobals::GameMonth()
{
	return Singleton()->gameMonth->data;
}

float TimeGlobals::GameHour()
{
	return Singleton()->gameHour->data;
}

UInt32 TimeGlobals::GameDaysPassed()
{
	return Singleton()->gameDaysPassed->data;
}

float TimeGlobals::TimeScale()
{
	return Singleton()->timeScale->data;
}

UInt32 TimeGlobals::HoursToRespawnCell()
{
	return *s_iHoursToRespawnCell;
}

UInt16 TimeGlobals::GetFirstDayOfMonth(UInt32 monthID)
{
	return (monthID - 1 < 12) ? s_firstDayOfMonths[monthID - 1] : -1;
}

UInt16 TimeGlobals::GetNumDaysInMonth(UInt32 monthID)
{
	return (monthID - 1 < 12) ? s_numDaysPerMonths[monthID - 1] : -1;
}

// Water Shader stuff
struct WaterShaderPropertyData {
	const char* name;
	UInt32		addr;
	bool		bIsPercentage;	// opacity and blend get multiplied by 100 for return value
};

static const UInt32 kNumWaterShaderProperties = 18;

WaterShaderPropertyData s_WaterShaderProperties[kNumWaterShaderProperties] =
{
	{	"direction",			0x00B45FC0,  false  },
	{	"velocity",				0x00B45FC4,  false  },

	{	"frequency",			0x00B45FD4,  false  },
	{	"amplitude",			0x00B45FD8,  false  },

	{	"fresnel",				0x00B45DC4,  false  },

	{	"reflectivity",			0x00B45E48,  false  },
	{	"opacity",				0x00B45E4C,  true   },
	{	"blend",				0x00B45E50,  true   },
	{	"scrollx",				0x00B45E54,  false  },
	{	"scrolly",				0x00B45E58,  false  },

	{	"rainforce",			0x00B45F58,  false  },
	{	"rainvelocity",			0x00B45F5C,  false  },
	{	"rainfalloff",			0x00B45F60,  false  },
	{	"rainsize",				0x00B45F64,  false  },
	{	"displaceforce",		0x00B45F68,  false  },
	{	"displacevelocity",		0x00B45F6C,  false  },
	{	"displacefalloff",		0x00B45F70,  false  },

	{	"displacedampener",		0x00B45F40,  false  },
};

TES** g_TES = (TES**)0x00B333A0;


GridCellArray::GridEntry* GridCellArray::GetGridEntry(UInt32 x, UInt32 y)
{
	return (GridEntry*)ThisStdCall(0x00482150, this, x, y);
}

TES* TES::GetSingleton()
{
	return *g_TES;
}

bool TES::GetTerrainHeight(float* posVec3, float* outHeight)
{
	return ThisStdCall(0x00440590, this, posVec3, outHeight) ? true : false;

}

bool GetWaterShaderProperty(const char* propName, float& out)
{
	bool bFound = false;

	if (propName)
	{
		for (UInt32 i = 0; i < kNumWaterShaderProperties; i++)
		{
			if (!_stricmp(propName, s_WaterShaderProperties[i].name))
			{
				bFound = true;
				out = *(float*)(s_WaterShaderProperties[i].addr);
				if (s_WaterShaderProperties[i].bIsPercentage)
					out *= 100;

				break;
			}
		}
	}

	return bFound;
}

#endif