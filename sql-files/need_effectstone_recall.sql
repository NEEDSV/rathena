-- NEED 2026-10 이펙트 스톤 회수 + 의상 인챈트 스톤 상자9 보상 (일회성)
--
-- 배경 : 의상 인챈트 스톤 상자9(23058)가 10/2 교환 목록에 올라가면서 환생 전용
--        이펙트(일렉트릭 중단)가 상자로 풀렸다. 모든 스톤 상자에서 이펙트를 제거하고,
--        상자9에서 나오던 이펙트 3종은 회수 후 상자로 돌려준다.
--
-- 회수 : 25138 슈링크 이펙트(중단) / 25136 일렉트릭 이펙트(중단) / 25137 그린 플로어 이펙트(하단)
--   (1) 아이템 : 인벤토리, 카트, 계정 창고 1~3(storage/storage2/storage3), 길드 창고, 미수령 우편
--   (2) 옷장 부여분 : 캐릭터 변수 COSTUME_EFFECT_MID = 29142(일렉트릭) / 29144(슈링크),
--                     COSTUME_EFFECT_LOW = 29143(그린 플로어) 를 지우고,
--                     옷장에서 꺼낸 의상(인벤토리/카트)의 card3 에 박힌 같은 카드를 뗀다.
--       부위는 Costume.txt L_PutCloset 과 같은 우선순위(상>중>하>걸칠것)로 판정한다.
--       (상단 29142 = 25206, 하단 29142 = 1000882, 하단 29144 = 25205 는 다른 스톤이라 건드리지 않는다)
-- 예외 : 25136 은 50회 환생 보상과 같은 아이템이다. NEED_REBIRTH_CLAIM_50 을 받은 캐릭터 수만큼
--        계정당 남긴다. 남기는 우선순위:
--        수령 캐릭터 옷장 부여분 → 수령 캐릭터 인벤토리 → 다른 캐릭터 옷장 부여분 → 다른 캐릭터 인벤토리
--        → 카트 → 계정 창고 → 우편 → 길드 창고
-- 보상 : 회수 1개(옷장 부여분 1건 포함)당 의상 인챈트 스톤 상자9 1개를 우편 발송 (@effectrecall)
--        길드 창고 회수분은 길드장 계정으로 지급한다.
--
-- 순서
--   1) 점검 시작, login/char/map 서버 전부 종료
--      (온라인 캐릭터의 인벤토리·창고·변수는 서버 메모리에 있다. 켜 둔 채 실행하면 저장 시 되살아난다)
--   2) DB 백업
--   3) 이 파일 실행 (재실행해도 회수는 한 번만 일어난다)
--   4) 서버 기동 후 인게임에서 @effectrecall 로 현황 확인 후 발송

CREATE TABLE IF NOT EXISTS `need_effectstone_recall_row` (
  `seq` int unsigned NOT NULL AUTO_INCREMENT,
  `src` varchar(16) NOT NULL COMMENT 'inventory/cart_inventory/storage/storage2/storage3/mail/guild_storage/closet_mid/closet_low',
  `row_id` bigint unsigned NOT NULL COMMENT '원본 테이블 id (mail 은 mail.id, closet_* 는 char_id)',
  `mail_index` tinyint unsigned NOT NULL DEFAULT 0,
  `account_id` int unsigned NOT NULL COMMENT '보상 받을 계정 (길드 창고는 길드장 계정)',
  `holder_id` int unsigned NOT NULL COMMENT 'char_id / account_id / guild_id',
  `nameid` int unsigned NOT NULL COMMENT '스톤 아이템 ID (옷장 부여분도 원래 스톤 ID로 기록)',
  `card_id` int unsigned NOT NULL DEFAULT 0 COMMENT '옷장 부여분의 카드 ID',
  `amount_before` int unsigned NOT NULL,
  `amount_removed` int unsigned NOT NULL,
  `bound` tinyint unsigned NOT NULL DEFAULT 0,
  `unique_id` bigint unsigned NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`seq`),
  KEY `account_id` (`account_id`),
  KEY `src_row` (`src`,`row_id`,`mail_index`)
) ENGINE=InnoDB;

-- 옷장에서 꺼낸 의상의 card3 를 뗀 기록 (되돌릴 때 사용)
CREATE TABLE IF NOT EXISTS `need_effectstone_recall_card` (
  `src` varchar(16) NOT NULL COMMENT 'inventory/cart_inventory',
  `row_id` bigint unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `nameid` int unsigned NOT NULL COMMENT '의상 아이템 ID',
  `card3_before` int unsigned NOT NULL,
  `equip` int unsigned NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`src`,`row_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_effectstone_recall` (
  `account_id` int unsigned NOT NULL,
  `char_id` int unsigned NOT NULL DEFAULT 0,
  `char_name` varchar(24) NOT NULL DEFAULT '',
  `removed_25136` int unsigned NOT NULL DEFAULT 0,
  `removed_25137` int unsigned NOT NULL DEFAULT 0,
  `removed_25138` int unsigned NOT NULL DEFAULT 0,
  `removed_closet` int unsigned NOT NULL DEFAULT 0 COMMENT '위 수량 중 옷장 부여분',
  `kept_25136` int unsigned NOT NULL DEFAULT 0,
  `box_amount` int unsigned NOT NULL DEFAULT 0,
  `status` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0=대기,1=발송완료,2=제외',
  `sent_at` datetime DEFAULT NULL,
  `note` varchar(64) NOT NULL DEFAULT '',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`),
  KEY `status` (`status`)
) ENGINE=InnoDB;

-- 옷장 부위표 (의상 도감 마스터 중 중단/하단으로 옷장에 들어가는 의상)
-- 생성: db/costume_collection_master.yml + db/re/item_db_equip.yml Locations, 우선순위 상>중>하>걸칠것
CREATE TABLE IF NOT EXISTS `need_effectstone_recall_part` (
  `item_id` int unsigned NOT NULL,
  `part` char(3) NOT NULL COMMENT 'MID/LOW',
  PRIMARY KEY (`item_id`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `need_effectstone_recall_part` (`item_id`,`part`) VALUES
  (5911,'MID'),(5912,'MID'),(5913,'MID'),(5914,'LOW'),(5977,'LOW'),(5979,'LOW'),(15840,'MID'),(15877,'MID'),(15953,'MID'),(15954,'LOW'),(18742,'MID'),(18744,'MID'),(19158,'MID'),(19287,'LOW'),(19289,'MID'),(19291,'MID'),(19293,'MID'),(19424,'MID'),(19459,'LOW'),(19500,'MID'),
  (19504,'MID'),(19505,'LOW'),(19509,'MID'),(19510,'MID'),(19511,'MID'),(19512,'MID'),(19513,'LOW'),(19514,'LOW'),(19528,'LOW'),(19531,'MID'),(19534,'MID'),(19541,'LOW'),(19542,'MID'),(19550,'MID'),(19551,'MID'),(19552,'LOW'),(19553,'LOW'),(19554,'MID'),(19563,'MID'),(19564,'MID'),
  (19566,'LOW'),(19583,'MID'),(19584,'LOW'),(19603,'MID'),(19604,'LOW'),(19605,'LOW'),(19606,'LOW'),(19609,'MID'),(19611,'MID'),(19615,'MID'),(19621,'MID'),(19624,'MID'),(19634,'LOW'),(19636,'LOW'),(19644,'MID'),(19651,'LOW'),(19672,'LOW'),(19674,'MID'),(19680,'MID'),(19683,'LOW'),
  (19692,'LOW'),(19713,'LOW'),(19722,'MID'),(19726,'LOW'),(19732,'MID'),(19734,'MID'),(19735,'MID'),(19736,'MID'),(19742,'MID'),(19752,'MID'),(19755,'MID'),(19764,'LOW'),(19765,'MID'),(19769,'MID'),(19770,'LOW'),(19776,'MID'),(19783,'LOW'),(19785,'LOW'),(19787,'MID'),(19788,'MID'),
  (19791,'MID'),(19792,'MID'),(19793,'MID'),(19794,'MID'),(19801,'MID'),(19805,'LOW'),(19810,'MID'),(19817,'LOW'),(19826,'MID'),(19846,'MID'),(19853,'MID'),(19871,'MID'),(19872,'MID'),(19877,'MID'),(19882,'MID'),(19885,'MID'),(19886,'MID'),(19887,'MID'),(19888,'MID'),(19889,'MID'),
  (19912,'MID'),(19918,'MID'),(19924,'MID'),(19925,'MID'),(19947,'LOW'),(19952,'LOW'),(19954,'MID'),(19989,'MID'),(19992,'LOW'),(19993,'LOW'),(19995,'MID'),(20010,'MID'),(20015,'LOW'),(20022,'LOW'),(20029,'LOW'),(20030,'LOW'),(20034,'LOW'),(20035,'LOW'),(20043,'LOW'),(20047,'MID'),
  (20054,'LOW'),(20059,'MID'),(20061,'LOW'),(20071,'LOW'),(20077,'MID'),(20091,'LOW'),(20107,'LOW'),(20108,'MID'),(20115,'MID'),(20116,'MID'),(20123,'MID'),(20124,'MID'),(20125,'MID'),(20126,'MID'),(20131,'MID'),(20132,'LOW'),(20136,'MID'),(20141,'MID'),(20142,'MID'),(20143,'LOW'),
  (20145,'MID'),(20146,'MID'),(20147,'MID'),(20149,'MID'),(20150,'LOW'),(20154,'MID'),(20156,'LOW'),(20166,'MID'),(20169,'LOW'),(20176,'MID'),(20192,'LOW'),(20193,'LOW'),(20195,'MID'),(20201,'LOW'),(20202,'LOW'),(20209,'MID'),(20215,'MID'),(20221,'MID'),(20223,'LOW'),(20230,'MID'),
  (20235,'LOW'),(20239,'LOW'),(20240,'LOW'),(20246,'MID'),(20255,'MID'),(20259,'LOW'),(20264,'LOW'),(20270,'MID'),(20279,'LOW'),(20280,'LOW'),(20285,'LOW'),(20286,'MID'),(20289,'MID'),(20295,'MID'),(20298,'MID'),(20299,'MID'),(20305,'LOW'),(20307,'LOW'),(20310,'LOW'),(20311,'MID'),
  (20312,'MID'),(20313,'LOW'),(20314,'MID'),(20315,'LOW'),(20318,'MID'),(20319,'MID'),(20321,'MID'),(20325,'MID'),(20326,'LOW'),(20328,'LOW'),(20335,'MID'),(20336,'LOW'),(20337,'MID'),(20340,'LOW'),(20341,'LOW'),(20342,'LOW'),(20344,'LOW'),(20345,'MID'),(20349,'LOW'),(20350,'LOW'),
  (20351,'LOW'),(20352,'LOW'),(20353,'LOW'),(20354,'LOW'),(20355,'LOW'),(20356,'LOW'),(20357,'LOW'),(20358,'LOW'),(20359,'LOW'),(20360,'LOW'),(20361,'LOW'),(20362,'LOW'),(20363,'LOW'),(20364,'LOW'),(20365,'LOW'),(20366,'LOW'),(20367,'LOW'),(20368,'LOW'),(20369,'LOW'),(20370,'LOW'),
  (20376,'MID'),(20379,'MID'),(20389,'MID'),(20390,'MID'),(20391,'MID'),(20397,'MID'),(20399,'MID'),(20400,'LOW'),(20404,'MID'),(20405,'LOW'),(20406,'MID'),(20407,'LOW'),(20416,'LOW'),(20417,'LOW'),(20418,'LOW'),(20419,'LOW'),(20420,'LOW'),(20421,'LOW'),(20422,'LOW'),(20423,'LOW'),
  (20424,'LOW'),(20425,'LOW'),(20426,'LOW'),(20427,'LOW'),(20429,'LOW'),(20430,'MID'),(20438,'LOW'),(20439,'MID'),(20440,'LOW'),(20441,'LOW'),(20448,'LOW'),(20457,'LOW'),(20459,'MID'),(20461,'MID'),(20462,'LOW'),(20465,'MID'),(20476,'LOW'),(20479,'MID'),(20486,'MID'),(20487,'LOW'),
  (20488,'MID'),(20492,'LOW'),(20493,'MID'),(20497,'LOW'),(20798,'LOW'),(21200,'MID'),(21202,'MID'),(21206,'MID'),(21207,'MID'),(31029,'LOW'),(31047,'MID'),(31055,'LOW'),(31056,'MID'),(31057,'LOW'),(31071,'LOW'),(31072,'LOW'),(31073,'LOW'),(31074,'LOW'),(31075,'LOW'),(31076,'LOW'),
  (31077,'LOW'),(31078,'LOW'),(31079,'LOW'),(31080,'LOW'),(31081,'LOW'),(31082,'LOW'),(31083,'LOW'),(31084,'LOW'),(31085,'LOW'),(31086,'LOW'),(31087,'LOW'),(31089,'MID'),(31093,'LOW'),(31096,'MID'),(31118,'MID'),(31120,'MID'),(31121,'LOW'),(31122,'MID'),(31129,'MID'),(31133,'MID'),
  (31146,'MID'),(31152,'LOW'),(31155,'MID'),(31162,'LOW'),(31164,'LOW'),(31167,'MID'),(31168,'MID'),(31178,'LOW'),(31181,'LOW'),(31184,'MID'),(31186,'MID'),(31187,'MID'),(31189,'LOW'),(31199,'LOW'),(31203,'MID'),(31207,'MID'),(31208,'LOW'),(31209,'LOW'),(31210,'LOW'),(31211,'LOW'),
  (31212,'LOW'),(31213,'LOW'),(31214,'LOW'),(31215,'LOW'),(31216,'LOW'),(31217,'LOW'),(31218,'LOW'),(31219,'LOW'),(31220,'LOW'),(31221,'LOW'),(31222,'LOW'),(31223,'LOW'),(31224,'LOW'),(31225,'LOW'),(31226,'LOW'),(31227,'LOW'),(31228,'LOW'),(31229,'LOW'),(31230,'LOW'),(31231,'LOW'),
  (31232,'LOW'),(31233,'LOW'),(31250,'MID'),(31251,'LOW'),(31257,'LOW'),(31258,'MID'),(31260,'MID'),(31261,'LOW'),(31292,'LOW'),(31296,'LOW'),(31299,'MID'),(31300,'LOW'),(31301,'MID'),(31302,'MID'),(31305,'LOW'),(31308,'MID'),(31310,'MID'),(31311,'MID'),(31313,'MID'),(31315,'LOW'),
  (31316,'LOW'),(31327,'MID'),(31330,'LOW'),(31374,'LOW'),(31375,'MID'),(31380,'MID'),(31381,'LOW'),(31383,'LOW'),(31384,'MID'),(31386,'LOW'),(31391,'MID'),(31393,'LOW'),(31395,'LOW'),(31398,'MID'),(31399,'MID'),(31403,'MID'),(31404,'LOW'),(31416,'MID'),(31418,'LOW'),(31421,'LOW'),
  (31422,'LOW'),(31423,'LOW'),(31424,'LOW'),(31425,'LOW'),(31426,'LOW'),(31427,'LOW'),(31428,'LOW'),(31429,'LOW'),(31432,'LOW'),(31437,'MID'),(31438,'LOW'),(31446,'LOW'),(31449,'MID'),(31450,'LOW'),(31452,'MID'),(31453,'LOW'),(31454,'LOW'),(31455,'MID'),(31460,'LOW'),(31461,'MID'),
  (31462,'MID'),(31463,'MID'),(31469,'LOW'),(31470,'LOW'),(31474,'LOW'),(31477,'LOW'),(31479,'LOW'),(31480,'MID'),(31482,'LOW'),(31483,'MID'),(31486,'MID'),(31487,'MID'),(31488,'MID'),(31490,'LOW'),(31491,'MID'),(31492,'LOW'),(31493,'LOW'),(31497,'MID'),(31498,'LOW'),(31500,'LOW'),
  (31503,'LOW'),(31505,'MID'),(31510,'LOW'),(31511,'LOW'),(31512,'MID'),(31514,'MID'),(31515,'MID'),(31517,'MID'),(31527,'MID'),(31532,'MID'),(31533,'LOW'),(31534,'MID'),(31540,'MID'),(31544,'LOW'),(31545,'LOW'),(31559,'LOW'),(31561,'MID'),(31563,'MID'),(31566,'LOW'),(31567,'LOW'),
  (31568,'MID'),(31569,'LOW'),(31570,'MID'),(31572,'LOW'),(31574,'MID'),(31575,'LOW'),(31580,'MID'),(31584,'LOW'),(31585,'LOW'),(31586,'LOW'),(31590,'LOW'),(31593,'LOW'),(31594,'LOW'),(31599,'LOW'),(31600,'MID'),(31601,'MID'),(31609,'MID'),(31611,'LOW'),(31614,'MID'),(31615,'MID'),
  (31616,'LOW'),(31617,'MID'),(31618,'MID'),(31619,'MID'),(31620,'MID'),(31621,'LOW'),(31625,'LOW'),(31626,'LOW'),(31630,'LOW'),(31635,'LOW'),(31636,'LOW'),(31637,'LOW'),(31638,'LOW'),(31639,'LOW'),(31640,'LOW'),(31641,'LOW'),(31642,'LOW'),(31643,'LOW'),(31644,'LOW'),(31645,'LOW'),
  (31646,'LOW'),(31647,'LOW'),(31648,'LOW'),(31649,'LOW'),(31650,'LOW'),(31651,'LOW'),(31652,'LOW'),(31653,'LOW'),(31654,'LOW'),(31655,'LOW'),(31656,'LOW'),(31657,'LOW'),(31658,'LOW'),(31659,'LOW'),(31660,'LOW'),(31670,'LOW'),(31671,'LOW'),(31672,'LOW'),(31673,'MID'),(31676,'LOW'),
  (31678,'LOW'),(31679,'LOW'),(31684,'LOW'),(31685,'LOW'),(31686,'MID'),(31687,'MID'),(31688,'MID'),(31692,'MID'),(31694,'LOW'),(31695,'LOW'),(31698,'LOW'),(31699,'MID'),(31700,'LOW'),(31706,'MID'),(31712,'LOW'),(31715,'MID'),(31716,'MID'),(31717,'LOW'),(31719,'LOW'),(31726,'LOW'),
  (31729,'LOW'),(31730,'LOW'),(31731,'LOW'),(31732,'LOW'),(31734,'MID'),(31735,'LOW'),(31762,'LOW'),(31763,'MID'),(31768,'LOW'),(31785,'MID'),(31787,'MID'),(31793,'LOW'),(31797,'LOW'),(31798,'MID'),(31803,'LOW'),(31814,'MID'),(31816,'LOW'),(31817,'LOW'),(31819,'LOW'),(31823,'MID'),
  (31824,'MID'),(31825,'LOW'),(31827,'LOW'),(31828,'MID'),(31830,'MID'),(31831,'LOW'),(31833,'LOW'),(31834,'LOW'),(31835,'LOW'),(31843,'MID'),(31844,'LOW'),(31846,'LOW'),(31847,'MID'),(31851,'MID'),(31853,'MID'),(31856,'LOW'),(31857,'LOW'),(31858,'LOW'),(31859,'LOW'),(31860,'LOW'),
  (31861,'LOW'),(31862,'LOW'),(31863,'LOW'),(31864,'LOW'),(31865,'LOW'),(31874,'LOW'),(31875,'LOW'),(31876,'LOW'),(31877,'LOW'),(31878,'LOW'),(31879,'LOW'),(31880,'LOW'),(31881,'LOW'),(31882,'LOW'),(31885,'LOW'),(31887,'LOW'),(31902,'MID'),(31905,'LOW'),(31910,'LOW'),(31911,'MID'),
  (31915,'LOW'),(31916,'LOW'),(31921,'LOW'),(31922,'LOW'),(31923,'MID'),(31924,'MID'),(31927,'LOW'),(31930,'MID'),(31931,'MID'),(31932,'LOW'),(31933,'LOW'),(31934,'MID'),(31936,'LOW'),(31938,'LOW'),(31940,'MID'),(31942,'MID'),(31944,'LOW'),(31946,'MID'),(31947,'MID'),(31948,'MID'),
  (31949,'MID'),(31951,'LOW'),(31953,'MID'),(31954,'MID'),(31957,'LOW'),(31959,'LOW'),(31960,'LOW'),(31961,'LOW'),(31962,'LOW'),(31963,'LOW'),(31964,'LOW'),(31965,'LOW'),(31966,'LOW'),(31967,'LOW'),(31968,'LOW'),(31969,'LOW'),(31970,'LOW'),(31971,'LOW'),(31973,'MID'),(31975,'LOW'),
  (400056,'LOW'),(400057,'LOW'),(400076,'LOW'),(400100,'LOW'),(400148,'LOW'),(400149,'MID'),(400258,'LOW'),(400327,'LOW'),(400423,'LOW'),(400425,'LOW'),(400427,'LOW'),(400491,'MID'),(400655,'LOW'),(400757,'LOW'),(400762,'LOW'),(400767,'LOW'),(400913,'MID'),(401295,'MID'),(410005,'MID'),(410011,'MID'),
  (410029,'MID'),(410030,'MID'),(410031,'MID'),(410032,'MID'),(410033,'MID'),(410034,'MID'),(410035,'MID'),(410036,'MID'),(410037,'MID'),(410038,'MID'),(410039,'MID'),(410040,'MID'),(410041,'MID'),(410042,'MID'),(410043,'MID'),(410044,'MID'),(410045,'MID'),(410047,'MID'),(410048,'MID'),(410049,'MID'),
  (410050,'MID'),(410051,'MID'),(410053,'MID'),(410054,'MID'),(410055,'MID'),(410056,'MID'),(410059,'MID'),(410060,'MID'),(410061,'MID'),(410062,'MID'),(410063,'MID'),(410068,'MID'),(410069,'MID'),(410072,'MID'),(410074,'MID'),(410077,'MID'),(410078,'MID'),(410081,'MID'),(410082,'MID'),(410083,'MID'),
  (410095,'MID'),(410098,'MID'),(410099,'MID'),(410103,'MID'),(410108,'MID'),(410111,'MID'),(410112,'MID'),(410113,'MID'),(410114,'MID'),(410115,'MID'),(410116,'MID'),(410117,'MID'),(410118,'MID'),(410121,'MID'),(410122,'MID'),(410123,'MID'),(410126,'MID'),(410127,'MID'),(410128,'MID'),(410131,'MID'),
  (410132,'MID'),(410133,'MID'),(410144,'MID'),(410145,'MID'),(410146,'MID'),(410147,'MID'),(410148,'MID'),(410149,'MID'),(410156,'MID'),(410157,'MID'),(410160,'MID'),(410169,'MID'),(410170,'MID'),(410178,'MID'),(410179,'MID'),(410182,'MID'),(410187,'MID'),(410188,'MID'),(410189,'MID'),(410190,'MID'),
  (410191,'MID'),(410192,'MID'),(410193,'MID'),(410196,'MID'),(410198,'MID'),(410199,'MID'),(410200,'MID'),(410201,'MID'),(410202,'MID'),(410205,'MID'),(410206,'MID'),(410212,'MID'),(410214,'MID'),(410217,'MID'),(410218,'MID'),(410219,'MID'),(410220,'MID'),(410221,'MID'),(410222,'MID'),(410223,'MID'),
  (410224,'MID'),(410226,'MID'),(410229,'MID'),(410231,'MID'),(410234,'MID'),(410236,'MID'),(410238,'MID'),(410239,'MID'),(410240,'MID'),(410241,'MID'),(410242,'MID'),(410245,'MID'),(410248,'MID'),(410251,'MID'),(410252,'MID'),(410253,'MID'),(410261,'MID'),(410262,'MID'),(410263,'MID'),(410264,'MID'),
  (410265,'LOW'),(410266,'MID'),(410267,'MID'),(410276,'MID'),(410277,'MID'),(410278,'MID'),(410279,'LOW'),(410280,'LOW'),(410282,'MID'),(410286,'MID'),(410288,'MID'),(410289,'MID'),(410290,'MID'),(410295,'MID'),(410296,'MID'),(410297,'MID'),(410308,'MID'),(410309,'MID'),(410310,'MID'),(410311,'MID'),
  (410312,'MID'),(410315,'MID'),(410316,'MID'),(410317,'MID'),(410320,'MID'),(410324,'MID'),(410325,'MID'),(410326,'MID'),(410328,'MID'),(410329,'MID'),(410330,'MID'),(410331,'MID'),(410338,'MID'),(410343,'MID'),(410350,'MID'),(410351,'MID'),(410352,'MID'),(410353,'MID'),(410357,'MID'),(410359,'MID'),
  (410363,'MID'),(410364,'MID'),(410367,'MID'),(410368,'MID'),(410371,'MID'),(410384,'MID'),(410385,'MID'),(410387,'MID'),(410388,'MID'),(410392,'MID'),(410393,'MID'),(410397,'MID'),(410399,'MID'),(410430,'MID'),(410440,'LOW'),(410441,'MID'),(410442,'MID'),(410447,'MID'),(410448,'MID'),(410456,'MID'),
  (410457,'MID'),(410458,'MID'),(410471,'MID'),(410474,'MID'),(410491,'MID'),(410508,'MID'),(410556,'MID'),(410574,'MID'),(410576,'MID'),(410582,'MID'),(420011,'LOW'),(420014,'LOW'),(420016,'MID'),(420023,'LOW'),(420025,'LOW'),(420026,'LOW'),(420027,'LOW'),(420029,'LOW'),(420033,'LOW'),(420034,'LOW'),
  (420035,'LOW'),(420036,'LOW'),(420037,'LOW'),(420038,'LOW'),(420039,'LOW'),(420040,'LOW'),(420041,'LOW'),(420042,'LOW'),(420043,'LOW'),(420044,'LOW'),(420046,'LOW'),(420047,'LOW'),(420048,'LOW'),(420050,'LOW'),(420051,'LOW'),(420053,'LOW'),(420054,'LOW'),(420057,'LOW'),(420058,'LOW'),(420059,'LOW'),
  (420060,'LOW'),(420061,'LOW'),(420062,'LOW'),(420067,'LOW'),(420069,'LOW'),(420071,'LOW'),(420072,'LOW'),(420073,'LOW'),(420077,'LOW'),(420079,'LOW'),(420081,'LOW'),(420082,'LOW'),(420083,'LOW'),(420084,'LOW'),(420085,'LOW'),(420086,'LOW'),(420088,'LOW'),(420091,'LOW'),(420092,'LOW'),(420094,'LOW'),
  (420103,'LOW'),(420104,'LOW'),(420107,'LOW'),(420108,'LOW'),(420109,'LOW'),(420111,'LOW'),(420113,'LOW'),(420114,'LOW'),(420115,'LOW'),(420116,'LOW'),(420117,'LOW'),(420118,'LOW'),(420119,'LOW'),(420120,'LOW'),(420121,'LOW'),(420122,'LOW'),(420123,'LOW'),(420124,'LOW'),(420125,'LOW'),(420126,'LOW'),
  (420127,'LOW'),(420128,'LOW'),(420132,'LOW'),(420133,'LOW'),(420140,'LOW'),(420150,'LOW'),(420151,'LOW'),(420156,'LOW'),(420161,'LOW'),(420162,'LOW'),(420163,'LOW'),(420165,'LOW'),(420169,'LOW'),(420170,'LOW'),(420171,'LOW'),(420172,'LOW'),(420177,'LOW'),(420181,'LOW'),(420190,'LOW'),(420191,'LOW'),
  (420192,'LOW'),(420193,'LOW'),(420197,'LOW'),(420201,'LOW'),(420204,'LOW'),(420205,'LOW'),(420207,'LOW'),(420208,'LOW'),(420211,'LOW'),(420212,'LOW'),(420218,'LOW'),(420219,'LOW'),(420221,'LOW'),(420222,'LOW'),(420224,'LOW'),(420232,'LOW'),(420233,'LOW'),(420234,'LOW'),(420237,'LOW'),(420240,'LOW'),
  (420241,'LOW'),(420242,'MID'),(420244,'LOW'),(420254,'LOW'),(420255,'LOW'),(420264,'LOW'),(420271,'LOW'),(420273,'LOW'),(420274,'LOW'),(420275,'LOW'),(420276,'LOW'),(420277,'LOW'),(420278,'LOW'),(420279,'LOW'),(420280,'LOW'),(420281,'LOW'),(420282,'LOW'),(420283,'LOW'),(420284,'LOW'),(420285,'LOW'),
  (420286,'LOW'),(420287,'LOW'),(420288,'LOW'),(420289,'LOW'),(420290,'LOW'),(420291,'LOW'),(420292,'LOW'),(420293,'LOW'),(420294,'LOW'),(420295,'LOW'),(420296,'LOW'),(420297,'LOW'),(420298,'LOW'),(420299,'LOW'),(420300,'LOW'),(420303,'LOW'),(420304,'LOW'),(420306,'LOW'),(420307,'LOW'),(420317,'LOW'),
  (420320,'LOW'),(420321,'LOW'),(420325,'LOW'),(420333,'LOW'),(420336,'LOW'),(420337,'LOW'),(420338,'LOW'),(420340,'LOW'),(420341,'LOW'),(420344,'LOW'),(420345,'LOW'),(420346,'LOW'),(420347,'LOW'),(420349,'LOW'),(420350,'LOW'),(420353,'LOW'),(420354,'LOW'),(420357,'LOW'),(420358,'LOW'),(420359,'LOW'),
  (420360,'LOW'),(420366,'LOW'),(420367,'LOW'),(420376,'LOW'),(420377,'LOW'),(420383,'LOW'),(420384,'LOW'),(420385,'LOW'),(420393,'LOW'),(420394,'LOW'),(420395,'LOW'),(420396,'LOW'),(420397,'LOW'),(420398,'LOW'),(420399,'LOW'),(420400,'LOW'),(420401,'LOW'),(420402,'LOW'),(420403,'LOW'),(420404,'LOW'),
  (420405,'LOW'),(420406,'LOW'),(420407,'LOW'),(420408,'LOW'),(420409,'LOW'),(420410,'LOW'),(420411,'LOW'),(420412,'LOW'),(420413,'LOW'),(420414,'LOW'),(420415,'LOW'),(420416,'LOW'),(420417,'LOW'),(420418,'LOW'),(420419,'LOW'),(420420,'LOW'),(420421,'LOW'),(420423,'LOW'),(420424,'LOW'),(420426,'LOW'),
  (420427,'LOW'),(420429,'LOW'),(420430,'LOW'),(420432,'MID'),(420433,'LOW'),(420434,'LOW'),(420435,'LOW'),(420439,'LOW'),(420440,'LOW'),(420447,'LOW'),(420448,'LOW'),(420449,'LOW'),(420451,'LOW'),(420499,'LOW'),(420500,'LOW'),(420501,'LOW'),(420511,'LOW'),(420512,'LOW'),(420514,'LOW'),(420515,'LOW'),
  (420516,'LOW'),(420524,'LOW'),(420525,'LOW'),(420532,'LOW'),(420545,'LOW'),(420546,'LOW'),(420547,'LOW'),(420552,'LOW'),(420553,'LOW'),(420554,'LOW'),(420570,'LOW'),(420571,'LOW'),(420575,'LOW'),(420576,'LOW'),(420594,'LOW'),(420595,'LOW'),(420596,'MID'),(420602,'LOW'),(420618,'LOW'),(420642,'LOW'),
  (420647,'LOW'),(420648,'LOW'),(420649,'LOW'),(420650,'LOW'),(420702,'LOW'),(420703,'LOW'),(420727,'LOW'),(420728,'LOW'),(420729,'LOW'),(420730,'LOW'),(420731,'LOW'),(420743,'LOW'),(420750,'LOW'),(420751,'LOW'),(420774,'LOW'),(420775,'LOW'),(420776,'LOW'),(436006,'MID'),(436007,'MID'),(436010,'MID'),
  (480200,'MID'),(480271,'LOW'),(480272,'LOW'),(480273,'LOW'),(480274,'LOW'),(480469,'LOW'),(480494,'LOW');

-- ------------------------------------------------------------------
-- 0) 사전 확인 : 서버가 꺼져 있어야 한다 (online 캐릭터 0 이어야 정상)
-- ------------------------------------------------------------------
SELECT COUNT(*) AS `online_chars(0이어야 함)` FROM `char` WHERE `online` <> 0;

-- ------------------------------------------------------------------
-- 1) 회수 대상 수집 (이미 수집했으면 건너뛴다 → 재실행 안전)
--    prio : 0 수령캐릭 옷장 / 1 수령캐릭 인벤 / 2 다른캐릭 옷장 / 3 다른캐릭 인벤
--           4 카트 / 5 계정 창고 / 6 우편 / 7 길드 창고
-- ------------------------------------------------------------------
SET @already := (SELECT COUNT(*) FROM `need_effectstone_recall_row`);

INSERT INTO `need_effectstone_recall_row`
  (`src`,`row_id`,`mail_index`,`account_id`,`holder_id`,`nameid`,`card_id`,`amount_before`,`amount_removed`,`bound`,`unique_id`)
SELECT r.src, r.row_id, r.mail_index, r.account_id, r.holder_id, r.nameid, r.card_id, r.amount,
       r.amount - CASE WHEN r.nameid = 25136
                       THEN LEAST(r.amount, GREATEST(0, COALESCE(k.keep, 0) - COALESCE(r.cum_before, 0)))
                       ELSE 0 END,
       r.bound, r.unique_id
FROM (
  SELECT u.*,
         SUM(u.amount) OVER (PARTITION BY u.account_id, u.nameid
                             ORDER BY u.prio, u.src, u.row_id, u.mail_index
                             ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING) AS cum_before
  FROM (
    SELECT CASE WHEN g.`key` = 'COSTUME_EFFECT_MID' THEN 'closet_mid' ELSE 'closet_low' END AS src,
           g.char_id AS row_id, 0 AS mail_index, c.account_id, g.char_id AS holder_id,
           CASE WHEN g.`key` = 'COSTUME_EFFECT_MID' AND g.`value` = 29142 THEN 25136
                WHEN g.`key` = 'COSTUME_EFFECT_MID' AND g.`value` = 29144 THEN 25138
                ELSE 25137 END AS nameid,
           g.`value` AS card_id, 1 AS amount, 0 AS bound, 0 AS unique_id,
           CASE WHEN EXISTS (SELECT 1 FROM `char_reg_num` q WHERE q.char_id = g.char_id
                             AND q.`key` = 'NEED_REBIRTH_CLAIM_50' AND q.`value` > 0) THEN 0 ELSE 2 END AS prio
    FROM `char_reg_num` g JOIN `char` c ON c.char_id = g.char_id
    WHERE g.`index` = 0
      AND ((g.`key` = 'COSTUME_EFFECT_MID' AND g.`value` IN (29142, 29144))
        OR (g.`key` = 'COSTUME_EFFECT_LOW' AND g.`value` = 29143))
    UNION ALL
    SELECT 'inventory', i.id, 0, c.account_id, i.char_id, i.nameid, 0, i.amount, i.bound, i.unique_id,
           CASE WHEN EXISTS (SELECT 1 FROM `char_reg_num` q WHERE q.char_id = i.char_id
                             AND q.`key` = 'NEED_REBIRTH_CLAIM_50' AND q.`value` > 0) THEN 1 ELSE 3 END
    FROM `inventory` i JOIN `char` c ON c.char_id = i.char_id
    WHERE i.nameid IN (25136, 25137, 25138)
    UNION ALL
    SELECT 'cart_inventory', ci.id, 0, c.account_id, ci.char_id, ci.nameid, 0, ci.amount, ci.bound, ci.unique_id, 4
    FROM `cart_inventory` ci JOIN `char` c ON c.char_id = ci.char_id
    WHERE ci.nameid IN (25136, 25137, 25138)
    UNION ALL
    SELECT 'storage', s.id, 0, s.account_id, s.account_id, s.nameid, 0, s.amount, s.bound, s.unique_id, 5
    FROM `storage` s WHERE s.nameid IN (25136, 25137, 25138)
    UNION ALL
    SELECT 'storage2', s.id, 0, s.account_id, s.account_id, s.nameid, 0, s.amount, s.bound, s.unique_id, 5
    FROM `storage2` s WHERE s.nameid IN (25136, 25137, 25138)
    UNION ALL
    SELECT 'storage3', s.id, 0, s.account_id, s.account_id, s.nameid, 0, s.amount, s.bound, s.unique_id, 5
    FROM `storage3` s WHERE s.nameid IN (25136, 25137, 25138)
    UNION ALL
    SELECT 'mail', a.id, a.`index`, c.account_id, m.dest_id, a.nameid, 0, a.amount, a.bound, a.unique_id, 6
    FROM `mail_attachments` a JOIN `mail` m ON m.id = a.id JOIN `char` c ON c.char_id = m.dest_id
    WHERE a.nameid IN (25136, 25137, 25138)
    UNION ALL
    SELECT 'guild_storage', gs.id, 0, c.account_id, gs.guild_id, gs.nameid, 0, gs.amount, gs.bound, gs.unique_id, 7
    FROM `guild_storage` gs JOIN `guild` gd ON gd.guild_id = gs.guild_id JOIN `char` c ON c.char_id = gd.char_id
    WHERE gs.nameid IN (25136, 25137, 25138)
  ) u
) r
LEFT JOIN (
  SELECT c.account_id, COUNT(*) AS keep
  FROM `char_reg_num` g JOIN `char` c ON c.char_id = g.char_id
  WHERE g.`key` = 'NEED_REBIRTH_CLAIM_50' AND g.`value` > 0
  GROUP BY c.account_id
) k ON k.account_id = r.account_id
WHERE @already = 0;

-- ------------------------------------------------------------------
-- 2) 회수 적용 (보상 대상 등록 전 1회만. 원본이 수집 때와 같을 때만 건드린다)
-- ------------------------------------------------------------------
SET @applied := (SELECT COUNT(*) FROM `need_effectstone_recall`);

-- 2-1) 옷장에서 꺼낸 의상의 card3 떼기
--      대상 = 인벤토리/카트의 중단 의상 card3 29142/29144, 하단 의상 card3 29143
--      단, 같은 캐릭터의 같은 부위·카드 옷장 부여분을 남기는 경우(50회 보상)는 제외
INSERT IGNORE INTO `need_effectstone_recall_card` (`src`,`row_id`,`char_id`,`nameid`,`card3_before`,`equip`)
SELECT x.src, x.id, x.char_id, x.nameid, x.card3, x.equip
FROM (
  SELECT 'inventory' AS src, i.id, i.char_id, i.nameid, i.card3, i.equip FROM `inventory` i
  WHERE i.card3 IN (29142, 29143, 29144)
  UNION ALL
  SELECT 'cart_inventory', ci.id, ci.char_id, ci.nameid, ci.card3, 0 FROM `cart_inventory` ci
  WHERE ci.card3 IN (29142, 29143, 29144)
) x
JOIN `need_effectstone_recall_part` p ON p.item_id = x.nameid
WHERE @applied = 0
  AND ((p.part = 'MID' AND x.card3 IN (29142, 29144)) OR (p.part = 'LOW' AND x.card3 = 29143))
  AND NOT EXISTS (
    SELECT 1 FROM `need_effectstone_recall_row` kr
    WHERE kr.src = CASE p.part WHEN 'MID' THEN 'closet_mid' ELSE 'closet_low' END
      AND kr.holder_id = x.char_id AND kr.card_id = x.card3 AND kr.amount_removed = 0);

UPDATE `inventory` t JOIN `need_effectstone_recall_card` rc ON rc.src = 'inventory' AND rc.row_id = t.id
SET t.card3 = 0
WHERE @applied = 0 AND t.card3 = rc.card3_before AND t.nameid = rc.nameid;
UPDATE `cart_inventory` t JOIN `need_effectstone_recall_card` rc ON rc.src = 'cart_inventory' AND rc.row_id = t.id
SET t.card3 = 0
WHERE @applied = 0 AND t.card3 = rc.card3_before AND t.nameid = rc.nameid;

-- 2-2) 옷장 부여 세팅(캐릭터 변수) 지우기
DELETE g FROM `char_reg_num` g
JOIN `need_effectstone_recall_row` r
  ON r.src IN ('closet_mid', 'closet_low') AND r.holder_id = g.char_id AND r.amount_removed = 1
WHERE @applied = 0 AND g.`index` = 0
  AND g.`key` = CASE r.src WHEN 'closet_mid' THEN 'COSTUME_EFFECT_MID' ELSE 'COSTUME_EFFECT_LOW' END
  AND g.`value` = r.card_id;

-- 2-3) 아이템 일부 회수 (25136 을 일부만 남기는 행)
UPDATE `inventory` t JOIN `need_effectstone_recall_row` r ON r.src='inventory' AND r.row_id=t.id
SET t.amount = t.amount - r.amount_removed
WHERE @applied = 0 AND r.amount_removed > 0 AND r.amount_removed < r.amount_before AND t.amount = r.amount_before;
UPDATE `cart_inventory` t JOIN `need_effectstone_recall_row` r ON r.src='cart_inventory' AND r.row_id=t.id
SET t.amount = t.amount - r.amount_removed
WHERE @applied = 0 AND r.amount_removed > 0 AND r.amount_removed < r.amount_before AND t.amount = r.amount_before;
UPDATE `storage` t JOIN `need_effectstone_recall_row` r ON r.src='storage' AND r.row_id=t.id
SET t.amount = t.amount - r.amount_removed
WHERE @applied = 0 AND r.amount_removed > 0 AND r.amount_removed < r.amount_before AND t.amount = r.amount_before;
UPDATE `storage2` t JOIN `need_effectstone_recall_row` r ON r.src='storage2' AND r.row_id=t.id
SET t.amount = t.amount - r.amount_removed
WHERE @applied = 0 AND r.amount_removed > 0 AND r.amount_removed < r.amount_before AND t.amount = r.amount_before;
UPDATE `storage3` t JOIN `need_effectstone_recall_row` r ON r.src='storage3' AND r.row_id=t.id
SET t.amount = t.amount - r.amount_removed
WHERE @applied = 0 AND r.amount_removed > 0 AND r.amount_removed < r.amount_before AND t.amount = r.amount_before;
UPDATE `mail_attachments` t JOIN `need_effectstone_recall_row` r ON r.src='mail' AND r.row_id=t.id AND r.mail_index=t.`index`
SET t.amount = t.amount - r.amount_removed
WHERE @applied = 0 AND r.amount_removed > 0 AND r.amount_removed < r.amount_before AND t.amount = r.amount_before;
UPDATE `guild_storage` t JOIN `need_effectstone_recall_row` r ON r.src='guild_storage' AND r.row_id=t.id
SET t.amount = t.amount - r.amount_removed
WHERE @applied = 0 AND r.amount_removed > 0 AND r.amount_removed < r.amount_before AND t.amount = r.amount_before;

-- 2-4) 아이템 전량 회수
DELETE t FROM `inventory` t JOIN `need_effectstone_recall_row` r ON r.src='inventory' AND r.row_id=t.id
WHERE @applied = 0 AND r.amount_removed = r.amount_before AND t.nameid = r.nameid AND t.amount = r.amount_before;
DELETE t FROM `cart_inventory` t JOIN `need_effectstone_recall_row` r ON r.src='cart_inventory' AND r.row_id=t.id
WHERE @applied = 0 AND r.amount_removed = r.amount_before AND t.nameid = r.nameid AND t.amount = r.amount_before;
DELETE t FROM `storage` t JOIN `need_effectstone_recall_row` r ON r.src='storage' AND r.row_id=t.id
WHERE @applied = 0 AND r.amount_removed = r.amount_before AND t.nameid = r.nameid AND t.amount = r.amount_before;
DELETE t FROM `storage2` t JOIN `need_effectstone_recall_row` r ON r.src='storage2' AND r.row_id=t.id
WHERE @applied = 0 AND r.amount_removed = r.amount_before AND t.nameid = r.nameid AND t.amount = r.amount_before;
DELETE t FROM `storage3` t JOIN `need_effectstone_recall_row` r ON r.src='storage3' AND r.row_id=t.id
WHERE @applied = 0 AND r.amount_removed = r.amount_before AND t.nameid = r.nameid AND t.amount = r.amount_before;
DELETE t FROM `mail_attachments` t JOIN `need_effectstone_recall_row` r ON r.src='mail' AND r.row_id=t.id AND r.mail_index=t.`index`
WHERE @applied = 0 AND r.amount_removed = r.amount_before AND t.nameid = r.nameid AND t.amount = r.amount_before;
DELETE t FROM `guild_storage` t JOIN `need_effectstone_recall_row` r ON r.src='guild_storage' AND r.row_id=t.id
WHERE @applied = 0 AND r.amount_removed = r.amount_before AND t.nameid = r.nameid AND t.amount = r.amount_before;

-- ------------------------------------------------------------------
-- 3) 계정별 보상 대상 등록 (수령 캐릭터 = 마지막 접속 캐릭터, 삭제 대기 캐릭터 제외)
-- ------------------------------------------------------------------
INSERT IGNORE INTO `need_effectstone_recall`
  (`account_id`,`char_id`,`char_name`,`removed_25136`,`removed_25137`,`removed_25138`,`removed_closet`,`kept_25136`,`box_amount`,`status`,`note`)
SELECT s.account_id,
       COALESCE(rc.char_id, 0), COALESCE(rc.name, ''),
       s.r36, s.r37, s.r38, s.rcl, s.k36, s.r36 + s.r37 + s.r38,
       CASE WHEN s.r36 + s.r37 + s.r38 = 0 THEN 2 WHEN rc.char_id IS NULL THEN 2 ELSE 0 END,
       CASE WHEN s.r36 + s.r37 + s.r38 = 0 THEN 'KEPT_ONLY' WHEN rc.char_id IS NULL THEN 'NO_CHAR' ELSE '' END
FROM (
  SELECT account_id,
         SUM(CASE WHEN nameid = 25136 THEN amount_removed ELSE 0 END) AS r36,
         SUM(CASE WHEN nameid = 25137 THEN amount_removed ELSE 0 END) AS r37,
         SUM(CASE WHEN nameid = 25138 THEN amount_removed ELSE 0 END) AS r38,
         SUM(CASE WHEN src IN ('closet_mid', 'closet_low') THEN amount_removed ELSE 0 END) AS rcl,
         SUM(CASE WHEN nameid = 25136 THEN amount_before - amount_removed ELSE 0 END) AS k36
  FROM `need_effectstone_recall_row`
  GROUP BY account_id
) s
LEFT JOIN (
  SELECT c.account_id, c.char_id, c.name,
         ROW_NUMBER() OVER (PARTITION BY c.account_id ORDER BY c.last_login DESC, c.char_id ASC) AS rn
  FROM `char` c WHERE c.delete_date = 0
) rc ON rc.account_id = s.account_id AND rc.rn = 1;

-- ------------------------------------------------------------------
-- 4) 확인
-- ------------------------------------------------------------------
-- 원본에 남은 스톤 수량 (25136 은 남긴 수량 중 아이템분과 같아야 하고, 25137/25138 은 0 이어야 한다)
SELECT x.nameid, SUM(x.amount) AS `남은수량`
FROM (
  SELECT nameid, amount FROM `inventory` WHERE nameid IN (25136,25137,25138)
  UNION ALL SELECT nameid, amount FROM `cart_inventory` WHERE nameid IN (25136,25137,25138)
  UNION ALL SELECT nameid, amount FROM `storage` WHERE nameid IN (25136,25137,25138)
  UNION ALL SELECT nameid, amount FROM `storage2` WHERE nameid IN (25136,25137,25138)
  UNION ALL SELECT nameid, amount FROM `storage3` WHERE nameid IN (25136,25137,25138)
  UNION ALL SELECT nameid, amount FROM `mail_attachments` WHERE nameid IN (25136,25137,25138)
  UNION ALL SELECT nameid, amount FROM `guild_storage` WHERE nameid IN (25136,25137,25138)
) x GROUP BY x.nameid;

-- 남은 옷장 부여 세팅 (50회 보상으로 남긴 일렉트릭 중단만 있어야 한다)
SELECT `key`, `value`, COUNT(*) AS `캐릭터수` FROM `char_reg_num`
WHERE `index` = 0
  AND ((`key` = 'COSTUME_EFFECT_MID' AND `value` IN (29142, 29144))
    OR (`key` = 'COSTUME_EFFECT_LOW' AND `value` = 29143))
GROUP BY `key`, `value`;

-- 남은 의상 카드 (위 남긴 세팅을 가진 캐릭터의 중단 의상 29142 만 있어야 한다)
SELECT p.part, x.card3, COUNT(*) AS `아이템수`
FROM (
  SELECT nameid, card3 FROM `inventory` WHERE card3 IN (29142, 29143, 29144)
  UNION ALL SELECT nameid, card3 FROM `cart_inventory` WHERE card3 IN (29142, 29143, 29144)
) x JOIN `need_effectstone_recall_part` p ON p.item_id = x.nameid
WHERE (p.part = 'MID' AND x.card3 IN (29142, 29144)) OR (p.part = 'LOW' AND x.card3 = 29143)
GROUP BY p.part, x.card3;

SELECT SUM(`removed_25136`) AS `회수_일렉트릭`, SUM(`removed_25137`) AS `회수_그린플로어`,
       SUM(`removed_25138`) AS `회수_슈링크`, SUM(`removed_closet`) AS `그중_옷장부여분`,
       SUM(`kept_25136`) AS `환생보상_남김`, SUM(`box_amount`) AS `지급상자합계`
FROM `need_effectstone_recall`;

SELECT (SELECT COUNT(*) FROM `need_effectstone_recall_card`) AS `카드_뗀_의상수`;

SELECT `status`,
       CASE `status` WHEN 0 THEN '발송 대기' WHEN 1 THEN '발송 완료' ELSE '제외' END AS `상태`,
       COUNT(*) AS `계정수`, SUM(`box_amount`) AS `상자수`
FROM `need_effectstone_recall` GROUP BY `status`;

-- 회수가 적용되지 않은 아이템 행 (0 건이어야 정상. 수집 후 원본 수량이 바뀐 경우이므로 수동 확인)
SELECT r.* FROM `need_effectstone_recall_row` r
JOIN (
  SELECT 'inventory' AS src, id AS row_id, 0 AS mail_index, amount FROM `inventory` WHERE nameid IN (25136,25137,25138)
  UNION ALL SELECT 'cart_inventory', id, 0, amount FROM `cart_inventory` WHERE nameid IN (25136,25137,25138)
  UNION ALL SELECT 'storage', id, 0, amount FROM `storage` WHERE nameid IN (25136,25137,25138)
  UNION ALL SELECT 'storage2', id, 0, amount FROM `storage2` WHERE nameid IN (25136,25137,25138)
  UNION ALL SELECT 'storage3', id, 0, amount FROM `storage3` WHERE nameid IN (25136,25137,25138)
  UNION ALL SELECT 'mail', id, `index`, amount FROM `mail_attachments` WHERE nameid IN (25136,25137,25138)
  UNION ALL SELECT 'guild_storage', id, 0, amount FROM `guild_storage` WHERE nameid IN (25136,25137,25138)
) o ON o.src = r.src AND o.row_id = r.row_id AND o.mail_index = r.mail_index
WHERE r.amount_removed > 0 AND o.amount <> r.amount_before - r.amount_removed;
