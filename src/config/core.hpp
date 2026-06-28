// Copyright (c) rAthena Dev Teams - GNU GPL 라이선스
// 자세한 내용은 메인 폴더의 LICENCE 파일 참고

#ifndef CONFIG_CORE_HPP
#define CONFIG_CORE_HPP

/**
 * rAthena 설정 파일 (http://rathena.org)
 * 자세한 설정 가이드는 아래 링크 참고:
 * http://rathena.org/wiki/SRC/config/
 **/

#include <custom/defines_pre.hpp>

 /// @autolootid 리스트에 들어갈 수 있는 최대 아이템 개수
#define AUTOLOOTITEM_SIZE 10

/// atcommand 및 @warp 자동완성(추천) 최대 개수
#define MAX_SUGGESTIONS 10

/// 공식 이동 경로 시스템을 비활성화하려면 주석 처리
/// 공식 walkpath는 원거리 유닛이 장애물을 돌아서 공격하지 못하게 함
/// 예: 장애물을 돌아가야 공격 가능한 경우,
///     플레이어는 직접 이동 클릭을 해야 공격 가능
///     몬스터는 사거리 안에 들어오지만 공격 못하면 타겟을 놓아버림
/// 비활성화하면:
/// 서버가 자동으로 공격 가능한 위치까지 이동시켜줌
/// 또한 시전 중 대상이 장애물 뒤로 가도 스킬 실패가 발생하지 않음
#define OFFICIAL_WALKPATH

/// Cell Stack Limit 기능 활성화 (주석 해제 시)
/// 설정은 battle_config의 custom_cell_stack_limit 사용
/// BL_CHAR에 정의된 캐릭터에만 적용됨
//#define CELL_NOSTACK

/// 원형 범위 체크 사용 (주석 해제 시)
/// 기본적으로 서버 범위 체크는 사각형 방식 (예: range 4 → 9x9 영역)
/// 클라이언트는 항상 원형 체크를 사용함
/// 이걸 활성화하면 서버도 원형 체크로 변경됨 (더 현실적)
/// 단, 공식 동작 방식은 아님
//#define CIRCULAR_AREA

/// 길드/파티 귀속 아이템 시스템 비활성화하려면 주석 처리
/// 기본적으로 길드/파티 귀속 아이템은 자동 회수/삭제됨
#define BOUND_ITEMS

/// 실시간 서버 상태 (입출력 데이터, RAM 사용량) 표시 (주석 해제 시)
//#define SHOW_SERVER_STATS

/// 직업별 기본 HP/SP/AP 테이블 비활성화하려면 주석 처리
/// (job_basepoints.yml 사용 여부)
#define HP_SP_TABLES

/// VIP 시스템 활성화 (주석 해제 시)
//#define VIP_ENABLE

/// VIP 스크립트 변경 적용 여부 (VIP_ENABLE 필요)
/// 주요 효과:
/// - 비VIP 유저 제한 (예: 3차 전직 시 Reset Stone 필요)
/// - 장비 강화 비용 증가 등
/// ※ euRO 기준이며 iRO와 다름
#define VIP_SCRIPT 0

#ifdef VIP_ENABLE
#ifndef MIN_STORAGE
#define MIN_STORAGE 300 // 기본 창고 슬롯 수
#endif
#ifndef MAX_CHAR_VIP
#define MAX_CHAR_VIP 6 // MAX_CHARS보다 작아야 함
#endif
#else
#ifndef MIN_STORAGE
#define MIN_STORAGE MAX_STORAGE // VIP 비활성 시 최소 = 최대
#endif
#ifndef MAX_CHAR_VIP
#define MAX_CHAR_VIP 0
#endif
#endif

#ifndef MAX_CHAR_BILLING
#define MAX_CHAR_BILLING 0 // MAX_CHARS보다 작아야 함
#endif

/// 구식(deprecated) 스크립트 명령어 경고 비활성화하려면 주석 처리
#define SCRIPT_COMMAND_DEPRECATION

/// 구식(deprecated) 스크립트 상수 경고 비활성화하려면 주석 처리
#define SCRIPT_CONSTANT_DEPRECATION

// Windows XP 이하 지원 활성화 (주석 해제 시)
// 주의:
// XP는 32비트 tick 사용 → 약 49일마다 오버플로우 발생
// → OS 재부팅 필요
//#define DEPRECATED_WINDOWS_SUPPORT

// 지원되지 않는 컴파일러 사용 허용 (주석 해제 시)
// 주의:
// 최신 C++ 규칙을 완전히 따르지 않을 수 있음
// → 예기치 않은 문제 발생 가능
// → 해당 컴파일러 관련 문의 금지
//#define DEPRECATED_COMPILER_SUPPORT

/// Nemo 패치 ExtendCashShopPreview 사용 시 활성화
//#define ENABLE_CASHSHOP_PREVIEW_PATCH

/// Nemo 패치 ExtendOldCashShopPreview 사용 시 활성화
#define ENABLE_OLD_CASHSHOP_PREVIEW_PATCH

#if defined(_DEBUG) || defined(DEBUG)
#define DETAILED_LOADING_OUTPUT
#endif

/// 상세 로딩 로그 강제 비활성화 (주석 해제 시)
/// 많은 상태 메시지 출력이 줄어들어 맵 서버 부팅 속도 빨라짐
//#undef DETAILED_LOADING_OUTPUT

/**
 * 이 아래에는 설정 없음
 **/
#include "./packets.hpp"
#include "./renewal.hpp"
#include "./secure.hpp"
#include "./classes/general.hpp"

 /**
  * 상수는 마지막에 처리됨
  * (앞에서 수정된 내용들을 반영하기 위해)
  **/
#include "./const.hpp"

#include <custom/defines_post.hpp>

#endif /* CONFIG_CORE_HPP */
