
// Copyright (c) rAthena Dev Teams - GNU GPL 라이선스
// 자세한 내용은 메인 폴더의 LICENCE 파일 참고

#ifndef CONFIG_RENEWAL_HPP
#define CONFIG_RENEWAL_HPP

#define NEED_2017_SKILL_FORMULA

// 모든 리뉴얼 옵션을 비활성화하는 빠른 옵션 (./configure에서 사용)
//#define PRERE
#ifndef PRERE
/**
 * rAthena 설정 파일 (http://rathena.org)
 * 자세한 설정 가이드는 아래 링크 참고
 * http://rathena.org/wiki/SRC/config/
 **/

 /**
  * @INFO: 이 파일은 전반적인 리뉴얼 설정을 담당함
  * 직업별 설정은 /src/config/classes 폴더 참고
  **/

  /// 게임 리뉴얼 서버 모드
  /// (이 줄을 주석 처리하면 비활성화됨)
  ///
  /// 이 줄을 유지하면 리뉴얼 공식(데미지, 계산식 등)이 활성화됨
#define RENEWAL

/// 리뉴얼 캐스팅 시간
/// (이 줄을 주석 처리하면 비활성화됨)
///
/// 이 줄을 유지하면 리뉴얼 캐스팅 시간 공식이 적용되고
/// 고정 캐스팅 시간 보너스가 활성화됨
///
/// 기본 고정 캐스팅 시간은 conf/battle/skill.conf의
/// default_fixed_castrate에서 설정 (기본값 20%)
///
/// 캐스팅 시간은 두 가지로 나뉨:
/// - VCT (Variable Cast Time, 변동 캐스팅 시간)
/// - FCT (Fixed Cast Time, 고정 캐스팅 시간)
///
/// 기본적으로 FCT는 전체 캐스팅 시간의 20%
/// (일부 스킬은 예외)
///
/// - VCT는 DEX * 2 + INT에 의해 감소
/// - FCT는 스탯으로 감소되지 않고 장비/버프로만 감소
///
/// 예시:
/// 캐스팅 시간이 10초인 스킬이면
/// 8초는 감소 가능하지만
/// 나머지 2초는 FCT라 줄일 수 없음
#define RENEWAL_CAST

/// 리뉴얼 드롭률 알고리즘
/// (이 줄을 주석 처리하면 비활성화됨)
///
/// 이 줄을 유지하면 리뉴얼 드롭 계산식 적용
///
/// 플레이어 레벨과 몬스터 레벨 차이에 따라
/// 드롭률 보정이 적용됨
///
/// 참고:
/// http://irowiki.org/wiki/Drop_System#Level_Factor
#define RENEWAL_DROP

/// 리뉴얼 경험치 계산 알고리즘
/// (이 줄을 주석 처리하면 비활성화됨)
///
/// 이 줄을 유지하면 리뉴얼 경험치 계산식 적용
///
/// 플레이어 레벨과 몬스터 레벨 차이에 따라
/// 경험치 보정이 적용됨
#define RENEWAL_EXP

/// 리뉴얼 레벨 기반 데미지 보정
/// (이 줄을 주석 처리하면 비활성화됨)
///
/// 이 줄을 유지하면 일부 스킬에 대해
/// 캐릭터 Base 레벨에 따른 데미지 보정 적용
#define RENEWAL_LVDMG

/// 리뉴얼 ASPD (공격속도) [malufett]
/// (이 줄을 주석 처리하면 비활성화됨)
///
/// 이 줄을 유지하면 리뉴얼 ASPD 시스템 적용
///
/// 특징:
/// - 방패 착용 시 패널티 적용
/// - AGI 영향이 더 커짐
/// - 스킬/아이템 ASPD 증가 방식 변경
/// - 일부 ASPD 보너스는 중첩되지 않음
#define RENEWAL_ASPD

/// 리뉴얼 스탯 계산
/// (이 줄을 주석 처리하면 비활성화됨)
///
/// 이 줄을 유지하면 스탯 상승 계산 방식이
/// 리뉴얼 기준으로 적용됨
#define RENEWAL_STAT

#endif

#endif /* CONFIG_RENEWAL_HPP */
