// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef CONFIG_CONST_H
#define CONFIG_CONST_H

#include <common/cbasetypes.hpp>

/**
 * rAthena 설정 파일 (http://rathena.org)
 * 자세한 설정 가이드는 아래 링크 참고:
 * http://rathena.org/wiki/SRC/config/
 **/

 /**
  * @INFO: 이 파일은 코드를 더 매끄럽고 효율적으로 만들기 위한
  * 상수들을 담고 있음
  */

  /**
   * "정상성 검사"
   * 이상한 버그가 있는 상태로 컴파일되는 것을 막기 위한 검사
   **/
#if SECURE_NPCTIMEOUT_INTERVAL <= 0
	#error SECURE_NPCTIMEOUT_INTERVAL should be at least 1 (1s)
#endif
#if NPC_SECURE_TIMEOUT_INPUT < 0
	#error NPC_SECURE_TIMEOUT_INPUT cannot be lower than 0
#endif
#if NPC_SECURE_TIMEOUT_MENU < 0
	#error NPC_SECURE_TIMEOUT_MENU cannot be lower than 0
#endif
#if NPC_SECURE_TIMEOUT_NEXT < 0
	#error NPC_SECURE_TIMEOUT_NEXT cannot be lower than 0
#endif

/**
 * /db 폴더 안에서 리뉴얼 / 프리리뉴얼 전용 DB 파일 경로
 **/
#ifdef RENEWAL
	#define DBPATH "re/"
#else
	#define DBPATH "pre-re/"
#endif

#define DBIMPORT "import"

/**
 * DefType
 * 방어력 타입 정의
 **/
#ifdef RENEWAL
	typedef int16 defType;
	#define DEFTYPE_MIN SHRT_MIN
	#define DEFTYPE_MAX SHRT_MAX
#else
	typedef signed char defType;
	#define DEFTYPE_MIN CHAR_MIN
	#define DEFTYPE_MAX CHAR_MAX
#endif

/**
 * EXP 정의 타입
 */
typedef uint64 t_exp;

/// 플레이어의 최대 Base / Job EXP
#if PACKETVER >= 20170830
	const t_exp MAX_EXP = INT64_MAX;
#else
	const t_exp MAX_EXP = INT32_MAX;
#endif

/// 길드 최대 EXP
const t_exp MAX_GUILD_EXP = INT32_MAX;
/// 최대 Base Level 상태에서의 플레이어 최대 Base EXP
const t_exp MAX_LEVEL_BASE_EXP = 99999999;
/// 최대 Job Level 상태에서의 플레이어 최대 Job EXP
const t_exp MAX_LEVEL_JOB_EXP = 999999999;

/* 포인터 크기 보정
 * gcc 경고 여러 개를 해결하기 위한 처리
 */
#ifdef __64BIT__
	#define __64BPRTSIZE(y) (intptr)y
#else
	#define __64BPRTSIZE(y) y
#endif

 /* ATCMD_FUNC(mobinfo)에서 사용하는 HIT / FLEE 계산 */
#ifdef RENEWAL
	#define MOB_FLEE(mob) ( mob->lv + mob->status.agi + 100 )
	#define MOB_HIT(mob)  ( mob->lv + mob->status.dex + 175 )
#else
	#define MOB_FLEE(mob) ( mob->lv + mob->status.agi )
	#define MOB_HIT(mob)  ( mob->lv + mob->status.dex )
#endif

/* 리뉴얼의 레벨 기반 데미지 보정
 * 쉽게 끄고 켤 수 있도록 매크로로 만들어둔 것
 */
#ifdef RENEWAL_LVDMG
	#define RE_LVL_DMOD(val) \
		if( status_get_lv(src) > 99 && val > 0 ) \
			skillratio = skillratio * status_get_lv(src) / val;
	#define RE_LVL_MDMOD(val) \
		if( status_get_lv(src) > 99 && val > 0) \
			md.damage = md.damage * status_get_lv(src) / val;
	/* ranger traps special */
	#define RE_LVL_TMDMOD() \
		if( status_get_lv(src) > 99 ) \
			md.damage = md.damage * 150 / 100 + md.damage * status_get_lv(src) / 100;
#else
	#define RE_LVL_DMOD(val)
	#define RE_LVL_MDMOD(val)
	#define RE_LVL_TMDMOD()
#endif

 // 리뉴얼 변동 캐스팅 시간 감소
#ifdef RENEWAL_CAST
	#define VARCAST_REDUCTION(val){ \
		if( (varcast_r += val) != 0 && varcast_r >= 0 ) \
			time = time * (1 - (float)min(val, 100) / 100); \
	}
#endif

/**
 * 신규 캐릭터 기본 좌표
 * 해당 맵은 map-server에서 로드되어 있어야 함
 **/
#ifdef RENEWAL
    #define MAP_DEFAULT_NAME "iz_int"
    #define MAP_DEFAULT_X 18
    #define MAP_DEFAULT_Y 26
#else
    #define MAP_DEFAULT_NAME "new_1-1"
    #define MAP_DEFAULT_X 53
    #define MAP_DEFAULT_Y 111
#endif

 /**
  * 파일 끝
  **/
#endif /* CONFIG_CONST_H */
