
SELECT
    cl.time, 
    ch.name AS char_name,
    cl.dst_charname,
    cl.type,
    cl.src_map,
    cl.message
    
FROM chatlog cl
INNER JOIN `char` ch
    ON ch.char_id = cl.src_charid
WHERE cl.time >= CONCAT('2026-9-09', ' 20:49:0')
-- AND cl.`message` LIKE '%영어%'
ORDER BY cl.time DESC;