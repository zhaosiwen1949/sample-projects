#pragma once

#include <foundation/api_registry.h>

// Creates API struct pointers and a function that loads them all. It supports up to 125 APIs as
// parameters. If you run TM_LOAD_APIS like this:
//
// ```c
// TM_LOAD_APIS(load_all_apis,
//     tm_allocator_api,
//     tm_entity_api)
// ```
//
// then it will output this code:
//
// ```c
// static struct tm_allocator_api *tm_allocator_api;
// static struct tm_entity_api *tm_entity_api;
//
// static void load_all_apis(struct tm_api_registry_api* reg) {
//     tm_allocator_api = reg->get("tm_allocator_api");
//     tm_entity_api = reg->get("tm_entity_api");
// }
// ```
//
// Additionally, make sure to then run `load_all_apis` whenever you plugin loads.
#define TM_LOAD_APIS(load_func_name, ...) \
    TM_CALL_FOR_EACH(TM_API_DEF, __VA_ARGS__) \
    static void load_func_name(struct tm_api_registry_api* reg) {\
        TM_CALL_FOR_EACH(TM_API_GET, __VA_ARGS__)\
    }

// tm_docgen off

#define TM_API_DEF(def_name) static struct def_name *def_name;

#define TM_API_GET(get_name) get_name = reg->get(#get_name);

#define TM_GET_NTH_ARG(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16,  \
    _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, \
    _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, \
    _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, _65, _66, _67, _68, _69, _70, _71, _72, _73, \
    _74, _75, _76, _77, _78, _79, _80, _81, _82, _83, _84, _85, _86, _87, _88, _89, _90, _91, _92, \
    _93, _94, _95, _96, _97, _98, _99, _100, _101, _102, _103, _104, _105, _106, _107, _108, _109, \
    _110, _111, _112, _113, _114, _115, _116, _117, _118, _119, _120, _121, _122, _123, _124, N,   \
    ...) N

#define TM_EXP(x) x

#define TM_C_0(f, ...)
#define TM_C_1(f, x) f(x)
#define TM_C_2(f, x, ...) f(x) TM_C_1(f, __VA_ARGS__)
#define TM_C_3(f, x, ...) f(x) TM_EXP(TM_C_2(f, __VA_ARGS__))
#define TM_C_4(f, x, ...) f(x) TM_EXP(TM_C_3(f, __VA_ARGS__))
#define TM_C_5(f, x, ...) f(x) TM_EXP(TM_C_4(f, __VA_ARGS__))
#define TM_C_6(f, x, ...) f(x) TM_EXP(TM_C_5(f, __VA_ARGS__))
#define TM_C_7(f, x, ...) f(x) TM_EXP(TM_C_6(f, __VA_ARGS__))
#define TM_C_8(f, x, ...) f(x) TM_EXP(TM_C_7(f, __VA_ARGS__))
#define TM_C_9(f, x, ...) f(x) TM_EXP(TM_C_8(f, __VA_ARGS__))
#define TM_C_10(f, x, ...) f(x) TM_EXP(TM_C_9(f, __VA_ARGS__))
#define TM_C_11(f, x, ...) f(x) TM_EXP(TM_C_10(f, __VA_ARGS__))
#define TM_C_12(f, x, ...) f(x) TM_EXP(TM_C_11(f, __VA_ARGS__))
#define TM_C_13(f, x, ...) f(x) TM_EXP(TM_C_12(f, __VA_ARGS__))
#define TM_C_14(f, x, ...) f(x) TM_EXP(TM_C_13(f, __VA_ARGS__))
#define TM_C_15(f, x, ...) f(x) TM_EXP(TM_C_14(f, __VA_ARGS__))
#define TM_C_16(f, x, ...) f(x) TM_EXP(TM_C_15(f, __VA_ARGS__))
#define TM_C_17(f, x, ...) f(x) TM_EXP(TM_C_16(f, __VA_ARGS__))
#define TM_C_18(f, x, ...) f(x) TM_EXP(TM_C_17(f, __VA_ARGS__))
#define TM_C_19(f, x, ...) f(x) TM_EXP(TM_C_18(f, __VA_ARGS__))
#define TM_C_20(f, x, ...) f(x) TM_EXP(TM_C_19(f, __VA_ARGS__))
#define TM_C_21(f, x, ...) f(x) TM_EXP(TM_C_20(f, __VA_ARGS__))
#define TM_C_22(f, x, ...) f(x) TM_EXP(TM_C_21(f, __VA_ARGS__))
#define TM_C_23(f, x, ...) f(x) TM_EXP(TM_C_22(f, __VA_ARGS__))
#define TM_C_24(f, x, ...) f(x) TM_EXP(TM_C_23(f, __VA_ARGS__))
#define TM_C_25(f, x, ...) f(x) TM_EXP(TM_C_24(f, __VA_ARGS__))
#define TM_C_26(f, x, ...) f(x) TM_EXP(TM_C_25(f, __VA_ARGS__))
#define TM_C_27(f, x, ...) f(x) TM_EXP(TM_C_26(f, __VA_ARGS__))
#define TM_C_28(f, x, ...) f(x) TM_EXP(TM_C_27(f, __VA_ARGS__))
#define TM_C_29(f, x, ...) f(x) TM_EXP(TM_C_28(f, __VA_ARGS__))
#define TM_C_30(f, x, ...) f(x) TM_EXP(TM_C_29(f, __VA_ARGS__))
#define TM_C_31(f, x, ...) f(x) TM_EXP(TM_C_30(f, __VA_ARGS__))
#define TM_C_32(f, x, ...) f(x) TM_EXP(TM_C_31(f, __VA_ARGS__))
#define TM_C_33(f, x, ...) f(x) TM_EXP(TM_C_32(f, __VA_ARGS__))
#define TM_C_34(f, x, ...) f(x) TM_EXP(TM_C_33(f, __VA_ARGS__))
#define TM_C_35(f, x, ...) f(x) TM_EXP(TM_C_34(f, __VA_ARGS__))
#define TM_C_36(f, x, ...) f(x) TM_EXP(TM_C_35(f, __VA_ARGS__))
#define TM_C_37(f, x, ...) f(x) TM_EXP(TM_C_36(f, __VA_ARGS__))
#define TM_C_38(f, x, ...) f(x) TM_EXP(TM_C_37(f, __VA_ARGS__))
#define TM_C_39(f, x, ...) f(x) TM_EXP(TM_C_38(f, __VA_ARGS__))
#define TM_C_40(f, x, ...) f(x) TM_EXP(TM_C_39(f, __VA_ARGS__))
#define TM_C_41(f, x, ...) f(x) TM_EXP(TM_C_40(f, __VA_ARGS__))
#define TM_C_42(f, x, ...) f(x) TM_EXP(TM_C_41(f, __VA_ARGS__))
#define TM_C_43(f, x, ...) f(x) TM_EXP(TM_C_42(f, __VA_ARGS__))
#define TM_C_44(f, x, ...) f(x) TM_EXP(TM_C_43(f, __VA_ARGS__))
#define TM_C_45(f, x, ...) f(x) TM_EXP(TM_C_44(f, __VA_ARGS__))
#define TM_C_46(f, x, ...) f(x) TM_EXP(TM_C_45(f, __VA_ARGS__))
#define TM_C_47(f, x, ...) f(x) TM_EXP(TM_C_46(f, __VA_ARGS__))
#define TM_C_48(f, x, ...) f(x) TM_EXP(TM_C_47(f, __VA_ARGS__))
#define TM_C_49(f, x, ...) f(x) TM_EXP(TM_C_48(f, __VA_ARGS__))
#define TM_C_50(f, x, ...) f(x) TM_EXP(TM_C_49(f, __VA_ARGS__))
#define TM_C_51(f, x, ...) f(x) TM_EXP(TM_C_50(f, __VA_ARGS__))
#define TM_C_52(f, x, ...) f(x) TM_EXP(TM_C_51(f, __VA_ARGS__))
#define TM_C_53(f, x, ...) f(x) TM_EXP(TM_C_52(f, __VA_ARGS__))
#define TM_C_54(f, x, ...) f(x) TM_EXP(TM_C_53(f, __VA_ARGS__))
#define TM_C_55(f, x, ...) f(x) TM_EXP(TM_C_54(f, __VA_ARGS__))
#define TM_C_56(f, x, ...) f(x) TM_EXP(TM_C_55(f, __VA_ARGS__))
#define TM_C_57(f, x, ...) f(x) TM_EXP(TM_C_56(f, __VA_ARGS__))
#define TM_C_58(f, x, ...) f(x) TM_EXP(TM_C_57(f, __VA_ARGS__))
#define TM_C_59(f, x, ...) f(x) TM_EXP(TM_C_58(f, __VA_ARGS__))
#define TM_C_60(f, x, ...) f(x) TM_EXP(TM_C_59(f, __VA_ARGS__))
#define TM_C_61(f, x, ...) f(x) TM_EXP(TM_C_60(f, __VA_ARGS__))
#define TM_C_62(f, x, ...) f(x) TM_EXP(TM_C_61(f, __VA_ARGS__))
#define TM_C_63(f, x, ...) f(x) TM_EXP(TM_C_62(f, __VA_ARGS__))
#define TM_C_64(f, x, ...) f(x) TM_EXP(TM_C_63(f, __VA_ARGS__))
#define TM_C_65(f, x, ...) f(x) TM_EXP(TM_C_64(f, __VA_ARGS__))
#define TM_C_66(f, x, ...) f(x) TM_EXP(TM_C_65(f, __VA_ARGS__))
#define TM_C_67(f, x, ...) f(x) TM_EXP(TM_C_66(f, __VA_ARGS__))
#define TM_C_68(f, x, ...) f(x) TM_EXP(TM_C_67(f, __VA_ARGS__))
#define TM_C_69(f, x, ...) f(x) TM_EXP(TM_C_68(f, __VA_ARGS__))
#define TM_C_70(f, x, ...) f(x) TM_EXP(TM_C_69(f, __VA_ARGS__))
#define TM_C_71(f, x, ...) f(x) TM_EXP(TM_C_70(f, __VA_ARGS__))
#define TM_C_72(f, x, ...) f(x) TM_EXP(TM_C_71(f, __VA_ARGS__))
#define TM_C_73(f, x, ...) f(x) TM_EXP(TM_C_72(f, __VA_ARGS__))
#define TM_C_74(f, x, ...) f(x) TM_EXP(TM_C_73(f, __VA_ARGS__))
#define TM_C_75(f, x, ...) f(x) TM_EXP(TM_C_74(f, __VA_ARGS__))
#define TM_C_76(f, x, ...) f(x) TM_EXP(TM_C_75(f, __VA_ARGS__))
#define TM_C_77(f, x, ...) f(x) TM_EXP(TM_C_76(f, __VA_ARGS__))
#define TM_C_78(f, x, ...) f(x) TM_EXP(TM_C_77(f, __VA_ARGS__))
#define TM_C_79(f, x, ...) f(x) TM_EXP(TM_C_78(f, __VA_ARGS__))
#define TM_C_80(f, x, ...) f(x) TM_EXP(TM_C_79(f, __VA_ARGS__))
#define TM_C_81(f, x, ...) f(x) TM_EXP(TM_C_80(f, __VA_ARGS__))
#define TM_C_82(f, x, ...) f(x) TM_EXP(TM_C_81(f, __VA_ARGS__))
#define TM_C_83(f, x, ...) f(x) TM_EXP(TM_C_82(f, __VA_ARGS__))
#define TM_C_84(f, x, ...) f(x) TM_EXP(TM_C_83(f, __VA_ARGS__))
#define TM_C_85(f, x, ...) f(x) TM_EXP(TM_C_84(f, __VA_ARGS__))
#define TM_C_86(f, x, ...) f(x) TM_EXP(TM_C_85(f, __VA_ARGS__))
#define TM_C_87(f, x, ...) f(x) TM_EXP(TM_C_86(f, __VA_ARGS__))
#define TM_C_88(f, x, ...) f(x) TM_EXP(TM_C_87(f, __VA_ARGS__))
#define TM_C_89(f, x, ...) f(x) TM_EXP(TM_C_88(f, __VA_ARGS__))
#define TM_C_90(f, x, ...) f(x) TM_EXP(TM_C_89(f, __VA_ARGS__))
#define TM_C_91(f, x, ...) f(x) TM_EXP(TM_C_90(f, __VA_ARGS__))
#define TM_C_92(f, x, ...) f(x) TM_EXP(TM_C_91(f, __VA_ARGS__))
#define TM_C_93(f, x, ...) f(x) TM_EXP(TM_C_92(f, __VA_ARGS__))
#define TM_C_94(f, x, ...) f(x) TM_EXP(TM_C_93(f, __VA_ARGS__))
#define TM_C_95(f, x, ...) f(x) TM_EXP(TM_C_94(f, __VA_ARGS__))
#define TM_C_96(f, x, ...) f(x) TM_EXP(TM_C_95(f, __VA_ARGS__))
#define TM_C_97(f, x, ...) f(x) TM_EXP(TM_C_96(f, __VA_ARGS__))
#define TM_C_98(f, x, ...) f(x) TM_EXP(TM_C_97(f, __VA_ARGS__))
#define TM_C_99(f, x, ...) f(x) TM_EXP(TM_C_98(f, __VA_ARGS__))
#define TM_C_100(f, x, ...) f(x) TM_EXP(TM_C_99(f, __VA_ARGS__))
#define TM_C_101(f, x, ...) f(x) TM_EXP(TM_C_100(f, __VA_ARGS__))
#define TM_C_102(f, x, ...) f(x) TM_EXP(TM_C_101(f, __VA_ARGS__))
#define TM_C_103(f, x, ...) f(x) TM_EXP(TM_C_102(f, __VA_ARGS__))
#define TM_C_104(f, x, ...) f(x) TM_EXP(TM_C_103(f, __VA_ARGS__))
#define TM_C_105(f, x, ...) f(x) TM_EXP(TM_C_104(f, __VA_ARGS__))
#define TM_C_106(f, x, ...) f(x) TM_EXP(TM_C_105(f, __VA_ARGS__))
#define TM_C_107(f, x, ...) f(x) TM_EXP(TM_C_106(f, __VA_ARGS__))
#define TM_C_108(f, x, ...) f(x) TM_EXP(TM_C_107(f, __VA_ARGS__))
#define TM_C_109(f, x, ...) f(x) TM_EXP(TM_C_108(f, __VA_ARGS__))
#define TM_C_110(f, x, ...) f(x) TM_EXP(TM_C_109(f, __VA_ARGS__))
#define TM_C_111(f, x, ...) f(x) TM_EXP(TM_C_110(f, __VA_ARGS__))
#define TM_C_112(f, x, ...) f(x) TM_EXP(TM_C_111(f, __VA_ARGS__))
#define TM_C_113(f, x, ...) f(x) TM_EXP(TM_C_112(f, __VA_ARGS__))
#define TM_C_114(f, x, ...) f(x) TM_EXP(TM_C_113(f, __VA_ARGS__))
#define TM_C_115(f, x, ...) f(x) TM_EXP(TM_C_114(f, __VA_ARGS__))
#define TM_C_116(f, x, ...) f(x) TM_EXP(TM_C_115(f, __VA_ARGS__))
#define TM_C_117(f, x, ...) f(x) TM_EXP(TM_C_116(f, __VA_ARGS__))
#define TM_C_118(f, x, ...) f(x) TM_EXP(TM_C_117(f, __VA_ARGS__))
#define TM_C_119(f, x, ...) f(x) TM_EXP(TM_C_118(f, __VA_ARGS__))
#define TM_C_120(f, x, ...) f(x) TM_EXP(TM_C_119(f, __VA_ARGS__))
#define TM_C_121(f, x, ...) f(x) TM_EXP(TM_C_120(f, __VA_ARGS__))
#define TM_C_122(f, x, ...) f(x) TM_EXP(TM_C_121(f, __VA_ARGS__))
#define TM_C_123(f, x, ...) f(x) TM_EXP(TM_C_122(f, __VA_ARGS__))
#define TM_C_124(f, x, ...) f(x) TM_EXP(TM_C_123(f, __VA_ARGS__))
#define TM_C_125(f, x, ...) f(x) TM_EXP(TM_C_124(f, __VA_ARGS__))

#define TM_CALL_FOR_EACH(x, ...) \
TM_EXP(TM_GET_NTH_ARG(__VA_ARGS__, TM_C_125, TM_C_124, TM_C_123, TM_C_122, TM_C_121, TM_C_120,  \
    TM_C_119, TM_C_118, TM_C_117, TM_C_116, TM_C_115, TM_C_114, TM_C_113, TM_C_112, TM_C_111,   \
    TM_C_110, TM_C_109, TM_C_108, TM_C_107, TM_C_106, TM_C_105, TM_C_104, TM_C_103, TM_C_102,   \
    TM_C_101, TM_C_100, TM_C_99, TM_C_98, TM_C_97, TM_C_96, TM_C_95, TM_C_94, TM_C_93, TM_C_92, \
    TM_C_91, TM_C_90, TM_C_89, TM_C_88, TM_C_87, TM_C_86, TM_C_85, TM_C_84, TM_C_83, TM_C_82,   \
    TM_C_81, TM_C_80, TM_C_79, TM_C_78, TM_C_77, TM_C_76, TM_C_75, TM_C_74, TM_C_73, TM_C_72,   \
    TM_C_71, TM_C_70, TM_C_69, TM_C_68, TM_C_67, TM_C_66, TM_C_65, TM_C_64, TM_C_63, TM_C_62,   \
    TM_C_61, TM_C_60, TM_C_59, TM_C_58, TM_C_57, TM_C_56, TM_C_55, TM_C_54, TM_C_53, TM_C_52,   \
    TM_C_51, TM_C_50, TM_C_49, TM_C_48, TM_C_47, TM_C_46, TM_C_45, TM_C_44, TM_C_43, TM_C_42,   \
    TM_C_41, TM_C_40, TM_C_39, TM_C_38, TM_C_37, TM_C_36, TM_C_35, TM_C_34, TM_C_33, TM_C_32,   \
    TM_C_31, TM_C_30, TM_C_29, TM_C_28, TM_C_27, TM_C_26, TM_C_25, TM_C_24, TM_C_23, TM_C_22,   \
    TM_C_21, TM_C_20, TM_C_19, TM_C_18, TM_C_17, TM_C_16, TM_C_15, TM_C_14, TM_C_13, TM_C_12,   \
    TM_C_11, TM_C_10, TM_C_9, TM_C_8, TM_C_7, TM_C_6, TM_C_5, TM_C_4, TM_C_3, TM_C_2, TM_C_1,   \
    TM_C_0)(x, __VA_ARGS__))
