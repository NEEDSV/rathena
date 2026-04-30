// Copyright (c) rAthena Dev Teams - GNU GPL 라이선스
// 자세한 내용은 메인 폴더의 LICENCE 파일 참고

#ifndef CONFIG_SECURE_HPP
#define CONFIG_SECURE_HPP

/**
 * rAthena 설정 파일 (http://rathena.org)
 * 자세한 설정 가이드는 아래 링크 참고:
 * http://rathena.org/wiki/SRC/config/
 **/

/**
 * @INFO: 이 파일은 선택적인 보안 설정들을 포함함
 **/

/**
 * NPC 대화 타이머 (옵션)
 * 활성화 시, 유저가 일정 시간 동안 아무 입력이 없으면 NPC 대화가 자동으로 종료됨
 * - 타임아웃 발생 시, NPC 대화창의 다음/메뉴 버튼이 '닫기' 버튼으로 변경됨
 * 비활성화하려면 주석 처리
 **/
#define SECURE_NPCTIMEOUT

/**
 * 'input' 입력창이 표시된 후, 유휴 상태로 간주되기까지의 시간 (초)
 * 기본값: 180초
 **/
#define NPC_SECURE_TIMEOUT_INPUT 180

/**
 * 'menu' 선택창이 표시된 후, 유휴 상태로 간주되기까지의 시간 (초)
 * 기본값: 60초
 **/
#define NPC_SECURE_TIMEOUT_MENU 60

/**
 * 'next' 버튼이 표시된 후, 유휴 상태로 간주되기까지의 시간 (초)
 * 기본값: 60초
 **/
#define NPC_SECURE_TIMEOUT_NEXT 60

/**
 * (보안) NPC 대화 타이머 체크 주기
 * @조건: SECURE_NPCTIMEOUT 활성화 필요
 * 타임아웃 체크 최소 간격 (초)
 * 기본값: 1초
 **/
#define SECURE_NPCTIMEOUT_INTERVAL 1

#endif /* CONFIG_SECURE_HPP */
