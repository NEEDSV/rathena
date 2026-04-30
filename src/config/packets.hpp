// Copyright (c) rAthena Dev Teams - GNU GPL 라이선스
// 자세한 내용은 메인 폴더의 LICENCE 파일 참고

#ifndef CONFIG_PACKETS_HPP
#define CONFIG_PACKETS_HPP

/**
 * rAthena 설정 파일 (http://rathena.org)
 * 자세한 설정 가이드는 아래 링크 참고:
 * http://rathena.org/wiki/SRC/config/
 **/

#ifndef PACKETVER
 /// 이 줄은 수정하지 마세요!
 /// 클라이언트 버전 설정 방법:
 /// Windows: src\custom\defines_pre.hpp에 아래 추가
 ///   #define PACKETVER YYYYMMDD
 /// Linux: 위와 동일하거나 다음 명령 실행
 ///   ./configure --enable-packetver=YYYYMMDD
#define PACKETVER 20211103
#endif

#ifndef PACKETVER_RE
	/// 2015년 11월 이후 → RagexeRE만 지원
	/// 2018년 7월 이후 → Ragexe만 지원
#if ( PACKETVER > 20151104 && PACKETVER < 20180704 ) || ( PACKETVER >= 20200902 && PACKETVER <= 20211118 )
#define PACKETVER_RE
#endif
#endif

#ifndef PACKETVER_RE
#define PACKETVER_MAIN_NUM PACKETVER

// Sakray 서버 관련 정의 제거
#undef PACKETVER_RE
#undef PACKETVER_RE_NUM
#else
	// 기존 정의 제거
#undef PACKETVER_RE

#define PACKETVER_RE PACKETVER
#define PACKETVER_RE_NUM PACKETVER

// 메인 서버 관련 정의 제거
#undef PACKETVER_MAIN_NUM
#endif

#if PACKETVER >= 20110817
	/// 공식 패킷 암호화(난독화) 지원을 비활성화하려면 주석 처리
	/// 최소 지원 클라이언트: 2011-08-17
#ifndef PACKET_OBFUSCATION
#define PACKET_OBFUSCATION

// 아래 키들은 src/custom/defines_pre.hpp 또는 defines_post.hpp에 정의
//#define PACKET_OBFUSCATION_KEY1 <key1>
//#define PACKET_OBFUSCATION_KEY2 <key2>
//#define PACKET_OBFUSCATION_KEY3 <key3>

/// 클라이언트 측 암호화 누락 경고를 끄려면 주석 처리
#define PACKET_OBFUSCATION_WARN
#endif
#else
#if defined(PACKET_OBFUSCATION)
#error 너무 오래된 버전에서 패킷 암호화를 활성화했습니다. 최소 지원: 2011-08-17
#endif
#endif

/// 공식 길드 창고 스킬을 비활성화하려면 주석 처리
/// 활성화 시 길드 창고 크기 = 스킬 레벨 × 100
#if PACKETVER >= 20131223
#define OFFICIAL_GUILD_STORAGE
#endif

#ifndef DUMP_UNKNOWN_PACKET
	//#define DUMP_UNKNOWN_PACKET
#endif

#ifndef DUMP_INVALID_PACKET
	//#define DUMP_INVALID_PACKET
#endif

/**
 * 이 아래에는 설정 없음
 **/

 /// 해당 PACKETVER가 PINCODE 시스템을 지원하는지 체크
#define PACKETVER_SUPPORTS_PINCODE PACKETVER >= 20110309

/// 캐릭터 삭제 날짜 처리 방식 체크
/// (삭제 날짜 대신 남은 시간으로 표시하는 클라이언트)
#define PACKETVER_CHAR_DELETEDATE (PACKETVER > 20130000 && PACKETVER <= 20141022) || PACKETVER >= 20150513

/// 해당 PACKETVER가 캐시샵 할인 시스템을 지원하는지 체크
#define PACKETVER_SUPPORTS_SALES PACKETVER >= 20131223

/// 웹 서비스 사용 여부
#define WEB_SERVER_ENABLE PACKETVER > 20200300

#endif /* CONFIG_PACKETS_HPP */
