// Copyright (c) rAthena Dev Teams - GNU GPL 라이선스
// 자세한 내용은 메인 폴더의 LICENCE 파일 참고

#ifndef CONFIG_GENERAL_HPP
#define CONFIG_GENERAL_HPP

/**
 * rAthena 설정 파일 (http://rathena.org)
 * 자세한 설정 가이드는 아래 링크 참고:
 * http://rathena.org/wiki/SRC/config/
 **/

 /**
  * 기본 마법 반사 동작 방식
  * - 반사 시, 반사 데미지는 대상이 착용한 장비가 아니라
  *   시전자가 착용한 장비 기준으로 계산됨
  * - 비활성화하면, 데미지는 시전자가 착용한 장비가 아니라
  *   대상이 착용한 장비 기준으로 계산됨
  * @values 1 (활성화) 또는 0 (비활성화)
  **/
#define MAGIC_REFLECTION_TYPE 1

  /**
   * 이 지점 이후로는 설정 없음
   **/

#endif /* CONFIG_GENERAL_HPP */
