/**
 * Love From YuWu 2025.06.22
 * ✨ YuWuの背包魔法 ✨
 * fixed by lzup
 */

#include "efmod_core.hpp"
#include "TEFMod.hpp"
#include "BaseType.hpp"
#include "Logger.hpp"

// 全局组件
TEFMod::Logger* g_log;
TEFMod::TEFModAPI* g_api;

// 字段解析器
static TEFMod::Field<void*>* (*ParseObjField)(void*);
static TEFMod::Method<void>* (*ParseVoidMethod)(void*);
static TEFMod::Array<void*>* (*ParseObjArray)(void*);

/*
 * 背包字段
 * 手机端: Player.bank 为 InventoryStorage, 需读取其 item (Item[]) 字段
 */
static TEFMod::Field<void*>* g_pInventory;      // Player.inventory      (Item[])
static TEFMod::Field<void*>* g_pBank;           // Player.bank           (InventoryStorage)
static TEFMod::Field<void*>* g_pBank2;          // Player.bank2          (InventoryStorage)
static TEFMod::Field<void*>* g_pBank3;          // Player.bank3          (InventoryStorage)
static TEFMod::Field<void*>* g_pBank4;          // Player.bank4          (InventoryStorage)
static TEFMod::Field<void*>* g_pStorageItem;    // InventoryStorage.item (Item[])

/*
 * 装备效果应用方法
 */
static TEFMod::Method<void>* g_pApplyEquipFunc;    // ApplyEquipFunctional(int, Item)
static TEFMod::Method<void>* g_pGrantPrefixBonus;  // GrantPrefixBenefits(Item)
static TEFMod::Method<void>* g_pGrantArmorBonus;   // GrantArmorBenefits(Item)

/*
 * 原版 ResetEffects
 */
static void (*g_pOriginalResetEffects)(TEFMod::TerrariaInstance);

// Hook 转发函数声明
void ResetEffects_T(TEFMod::TerrariaInstance i);

// Hook 模板
inline TEFMod::HookTemplate HookTemplate_ResetEffects {
        reinterpret_cast<void*>(ResetEffects_T),
        {}
};

// 转发函数实现: 先调用原版, 再执行所有注册的钩子
void ResetEffects_T(TEFMod::TerrariaInstance i) {
    if (g_pOriginalResetEffects) g_pOriginalResetEffects(i);
    for (const auto fun : HookTemplate_ResetEffects.FunctionArray) {
        if (fun) reinterpret_cast<void(*)(TEFMod::TerrariaInstance)>(fun)(i);
    }
}

/**
 * 处理单个物品栏
 * @param player       玩家实例
 * @param pItems       物品数组 (Item[])
 * @param skipLastSlot 是否跳过最后一个槽位
 *                     (主背包第58格是鼠标/垃圾槽, 不参与装备效果)
 */
static void ProcessInventory(TEFMod::TerrariaInstance player,
                             TEFMod::Array<void*>* pItems,
                             bool skipLastSlot) {
    if (!pItems || pItems->Size() == 0) return;
    if (!g_pApplyEquipFunc || !g_pGrantPrefixBonus || !g_pGrantArmorBonus) return;

    int count = static_cast<int>(pItems->Size());
    if (skipLastSlot && count > 0) count -= 1;

    for (int i = 0; i < count; ++i) {
        void* pItem = pItems->at(i);
        if (!pItem) continue;

        // ApplyEquipFunctional 内部会访问 hideVisibleAccessory[itemSlot] (长度10)
        // 背包/银行槽位索引可能越界导致崩溃, 需限制在合法范围内
        int eqSlot = i;
        if (eqSlot < 0) eqSlot = 0;
        if (eqSlot > 9) eqSlot = 9;

        g_pApplyEquipFunc->Call(player, 2, eqSlot, pItem);
        g_pGrantPrefixBonus->Call(player, 1, pItem);
        g_pGrantArmorBonus->Call(player, 1, pItem);
    }
}

/**
 * 应用口袋装备效果
 * 处理主背包以及所有银行容器中的装备
 */
static void ApplyPocketEffects(TEFMod::TerrariaInstance player) {
    if (!g_pApplyEquipFunc || !g_pGrantPrefixBonus || !g_pGrantArmorBonus) {
        if (g_log) g_log->e("PocketGear", "methods not resolved, skip");
        return;
    }
    if (!player) return;

    // 主背包 (跳过鼠标槽)
    ProcessInventory(player, ParseObjArray(g_pInventory->Get(player)), true);

    // 各类银行容器 (Player.bank 是 InventoryStorage, 取 item 字段)
    ProcessInventory(player, ParseObjArray(g_pStorageItem->Get(g_pBank->Get(player))), false);
    ProcessInventory(player, ParseObjArray(g_pStorageItem->Get(g_pBank2->Get(player))), false);
    ProcessInventory(player, ParseObjArray(g_pStorageItem->Get(g_pBank3->Get(player))), false);
    ProcessInventory(player, ParseObjArray(g_pStorageItem->Get(g_pBank4->Get(player))), false);
}

/**
 * 口袋装备核心实现
 */
class PocketGear final : public EFMod {
public:
    int Initialize(const std::string &path, MultiChannel *multiChannel) override {
        return 0;
    }

    int UnLoad(const std::string &path, MultiChannel *multiChannel) override {
        return 0;
    }

    int Load(const std::string &path, MultiChannel* channel) override {
        // 初始化日志和API
        g_log = channel->receive<TEFMod::Logger*(*)(const std::string&, const std::string&, const std::size_t)>(
                "TEFMod::CreateLogger")("PocketGear", "", 0);
        g_api = channel->receive<TEFMod::TEFModAPI*>("TEFMod::TEFModAPI");
        if (!g_api) return 1;
        g_log->init();
        g_log->i("PocketGear", "mod loaded");
        return 0;
    }

    void Send(const std::string &path, MultiChannel* channel) override {
        // 注册 ResetEffects 钩子
        g_api->registerFunctionDescriptor({
                                                  "Terraria",
                                                  "Player",
                                                  "ResetEffects",
                                                  "hook>>void",
                                                  0,
                                                  &HookTemplate_ResetEffects,
                                                  { reinterpret_cast<void*>(ApplyPocketEffects) }
                                          });

        // 注册玩家背包字段 (手机端 bank 为 InventoryStorage)
        g_api->registerApiDescriptor({"Terraria", "Player", "inventory", "Field"});
        g_api->registerApiDescriptor({"Terraria", "Player", "bank", "Field"});
        g_api->registerApiDescriptor({"Terraria", "Player", "bank2", "Field"});
        g_api->registerApiDescriptor({"Terraria", "Player", "bank3", "Field"});
        g_api->registerApiDescriptor({"Terraria", "Player", "bank4", "Field"});

        // 注册背包物品数组字段
        g_api->registerApiDescriptor({"Terraria", "InventoryStorage", "item", "Field"});

        // 注册装备效果应用方法
        g_api->registerApiDescriptor({"Terraria", "Player", "ApplyEquipFunctional", "Method", 2});
        g_api->registerApiDescriptor({"Terraria", "Player", "GrantPrefixBenefits", "Method", 1});
        g_api->registerApiDescriptor({"Terraria", "Player", "GrantArmorBenefits", "Method", 1});
    }

    void Receive(const std::string &path, MultiChannel* channel) override {
        // 获取字段解析器
        ParseObjField = channel->receive<TEFMod::Field<void*>*(*)(void*)>(
                "TEFMod::Field<Other>::ParseFromPointer");
        ParseVoidMethod = channel->receive<TEFMod::Method<void>*(*)(void*)>(
                "TEFMod::Method<Void>::ParseFromPointer");
        ParseObjArray = channel->receive<TEFMod::Array<void*>*(*)(void*)>(
                "TEFMod::Array<Other>::ParseFromPointer");

        if (g_log) {
            g_log->i("PocketGear", "parsers: objField=", (void*)ParseObjField,
                     " voidMethod=", (void*)ParseVoidMethod,
                     " objArray=", (void*)ParseObjArray);
        }

        // 获取原版 ResetEffects
        g_pOriginalResetEffects = g_api->GetAPI<void(*)(TEFMod::TerrariaInstance)>({
            "Terraria", "Player", "ResetEffects", "old_fun", 0
        });

        // 初始化 InventoryStorage.item 字段 (银行物品数组)
        g_pStorageItem = ParseObjField(g_api->GetAPI<void*>({
            "Terraria", "InventoryStorage", "item", "Field"
        }));

        // 初始化主背包字段
        g_pInventory = ParseObjField(g_api->GetAPI<void*>({
            "Terraria", "Player", "inventory", "Field"
        }));

        // 初始化各类银行容器字段
        g_pBank = ParseObjField(g_api->GetAPI<void*>({
            "Terraria", "Player", "bank", "Field"
        }));
        g_pBank2 = ParseObjField(g_api->GetAPI<void*>({
            "Terraria", "Player", "bank2", "Field"
        }));
        g_pBank3 = ParseObjField(g_api->GetAPI<void*>({
            "Terraria", "Player", "bank3", "Field"
        }));
        g_pBank4 = ParseObjField(g_api->GetAPI<void*>({
            "Terraria", "Player", "bank4", "Field"
        }));

        // 初始化装备效果应用方法
        g_pApplyEquipFunc = ParseVoidMethod(g_api->GetAPI<void*>({
            "Terraria", "Player", "ApplyEquipFunctional", "Method", 2
        }));
        g_pGrantPrefixBonus = ParseVoidMethod(g_api->GetAPI<void*>({
            "Terraria", "Player", "GrantPrefixBenefits", "Method", 1
        }));
        g_pGrantArmorBonus = ParseVoidMethod(g_api->GetAPI<void*>({
            "Terraria", "Player", "GrantArmorBenefits", "Method", 1
        }));

        if (g_log) {
            g_log->i("PocketGear", "fields: inventory=", (void*)g_pInventory,
                     " bank=", (void*)g_pBank,
                     " storageItem=", (void*)g_pStorageItem);
            g_log->i("PocketGear", "methods: equip=", (void*)g_pApplyEquipFunc,
                     " prefix=", (void*)g_pGrantPrefixBonus,
                     " armor=", (void*)g_pGrantArmorBonus);
        }
    }

    Metadata GetMetadata() override {
        return {
                "口袋装备",  // Mod名称
                "雨鹜",      // 作者
                "1.2.0",     // 版本
                20250517,    // 标准规范(与经典EFMod API一致)
                ModuleType::Game,
                { false }
        };
    }
};

EFMod* CreateMod() {
    static PocketGear instance;
    return &instance;
}
