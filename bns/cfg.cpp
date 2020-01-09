/*
 * cfg.cpp
 *
 */
#include "bns.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>



#ifndef DEPLOY_LIVE

const bn_acc_desc cfg_accounts[] =
{
    { GT_REALM_USEAST, "bns", "dolphin&552", "bahram", },
};

const bn_key_desc cfg_keys[] =
{
    // bns28@laserblue.org
    "H8RHXHDR4YDMY9J766TRC6B8CF",
    "HJBFHGWDHP46M4GBNEGKR2FYMR"
};

#else

const bn_acc_desc cfg_accounts[] =
{
    { GT_REALM_USEAST, "bns-exsnn" },
    { GT_REALM_USEAST, "bns-exsnx" },
    { GT_REALM_USEAST, "bns-exsnh" },
    
    { GT_REALM_USEAST, "bns-exsln" },
    { GT_REALM_USEAST, "bns-exslx" },
    { GT_REALM_USEAST, "bns-exslh" },

    { GT_REALM_USEAST, "bns-exhnn" },
    { GT_REALM_USEAST, "bns-exhln" },
    { GT_REALM_USEAST, "bns-echln" },
    { GT_REALM_USEAST, "bns-echnn" },
      
    { GT_REALM_USWEST, "bns-wxsln" },
    { GT_REALM_USWEST, "bns-wxslx" },
    { GT_REALM_USWEST, "bns-wxslh" },

    { GT_REALM_USWEST, "bns-wxsnn" },
    { GT_REALM_USWEST, "bns-wxsnx" },
    { GT_REALM_USWEST, "bns-wxsnh" },

    { GT_REALM_USWEST, "bns-wxhnn" },
    { GT_REALM_USWEST, "bns-wxhln" },
      
    { GT_REALM_EUROPE, "bns-uxsln" },
    { GT_REALM_EUROPE, "bns-uxslx" },
    { GT_REALM_EUROPE, "bns-uxslh" },
      
    { GT_REALM_EUROPE, "bns-uxsnn" },
    { GT_REALM_EUROPE, "bns-uxsnx" },
    { GT_REALM_EUROPE, "bns-uxsnh" },
    
    { GT_REALM_EUROPE, "bns-uxhln" },
    { GT_REALM_EUROPE, "bns-uxhnn" },
};



const bn_key_desc cfg_keys[] =
{
    // bilricker8878@gmail.com / Harmondale1_"
    "EH9MH84PWMNPJ4KRNWTGG4BHGY",
    "WF6MJHR46KFHKWH9JF4G49V69X",

    // bzxbns1@hotmail.com"
    "HZG2D6FNPG2KR6K8DVFP7THKMP",
    "YDZ8EN9FJ6ZHHHWWZ6HMG9CDXT",

    // bns1@laserblue.org"
    "TGDRNTJZRKW4ZEJ88RGGW4BJXG",
    "7G4HGB7DDNTFRBVX82EGEP4FJK",

    // bns2@laserblue.org"
    "278GVX2FD8YKDEPEF6VXG72WX4",
    "B9FJR8CEY9WCDDWMJBJDCZX72J",

    // bns3@laserblue.org"
    "MZMMGBXKWRH8EDFEG96WT6NV2P",
    "JJG8CJEBCBG2RMNK8949V4FJWP",

    // bns4@laserblue.org"
    "JCBT2G2XVJE9CJTY8B66KFFWVJ",
    "8E7PBR222TCT9D8BVWEPJCGH8K",

    // bns5@laserblue.org"
    "PXBZE7VRZ9EY6EMBKJR6PKWCJ7",
    "H6WHNEXE7NPZ7FPK7VYPHNBCRE",

    // bns6@laserblue.org"
    "PGYZT2XCPGPXTN9XKPEPDJVGT9",
    "MM2NMXRVRGVDEH8DGHXHG4CFBN",

    // bns7@laserblue.org"
    "8MZNM89FJMZBERP24H8CGWX6Z4",
    "9BYMKPXRJ9G4ZRBHG92E4224E9",

    // bns8@laserblue.org"
    "7PTT2XZ8TDPBMEE6ZGP2FWCVX9",
    "6Z26C6KEH9N26KCMHXPKHV2KHZ",

    // bns9@laserblue.org"
    "WFWB969XTEYZVRRFGZ6RV9XTCP",
    "FTMXK46ZKRFPPWJKTKHYYY9GK4",

    // bns10@laserblue.org"
    "2CXZE2EM9E4NBB4TWFZ99GXNJX",
    "EYC7P4KKFXTVJVHRKR8KNV9X47",

    // bns11@laserblue.org"
    "9BZEBP4BH2KH7WK4X7ZYMKC7JC",
    "BG28ZEZGWKYBXCTYGK9CRJCNNE",

    // bns12@laserblue.org"
    "H6N2YZ77TZGJVRJNV6CXPDCDTV",
    "DJ7K7MJPD4NY7894DDG8CXHR46",

    // bns13@laserblue.org"
    "JNYGV4XFHZPV9H7MZZ89FGGGYT",
    "GBTFWE4HTKRTEPBDC9XPCV6KDG",

    // bns14@laserblue.org"
    "6JNPYG8DDJ2J9ZE8Z2XJGR2V2Y",
    "BEDYWXN6KYBE9JX2TDC94GC46G",

    // bns15@laserblue.org"
    "D6TMYTJ277HCJJTDZXD7FZ26XX",
    "KZV7VPZJJ4HEF8RX9DC9MJ96BW",

    // bns16@laserblue.org"
    "HT7WXDBRCWJZFEDD7MB7D7M6BK",
    "FH6GXMNZ6DGMY8DGFN4J9G6RJ7",

    // bns17@laserblue.org"
    "V7KCXTVXPMPVN7WZ79RT6P9G8R",
    "9GD72WCEDFJ22E264YT4HXCTXB",

    // bns18@laserblue.org"
    "7WZ8FFH7H7F77K98PM2PRPHKJD",
    "TZ2DDTNY2EMDPZ2Z7DFC7CXMTG",

    // bns19@laserblue.org"
    "2HCD9RDWZJZFJPNRKD9KTZPEHZ",
    "9KBK4DNY9GGX7NWDGDEJE2XKWC",

    // bns20@laserblue.org
    "RKCNYVGD6R4X86BPEEPRWCGKGG",
    "WHWT888HVBNN8TCKZCG7TB9T8P",

    // bns21@laserblue.org
    "8ZYRGY7XM86DFM9XYDJY8GNFXZ",
    "C22NYYRCEXCCR9KBHMM9CX4HMZ",

    // bns22@laserblue.org
    "TRBFYJBXN4Z96BKD4TE9KDF4PN",
    "DZVTWVR9YY94M76VT7KCTXB2WZ",

    // bns23@laserblue.org
    "ZZMFHDBTVPJWRTM27K4NH8M2YC",
    "XEP7D24N882TKP82RECRFJ22P9",

    // bns24@laserblue.org
    "T8TN9M7GVRTW67CNEMZDXZG4T9",
    "YV2E9X6JT4H7WBVB9E7EYPC7YD",

    // bns25@laserblue.org
    "7JV6HKK87PXVPPCZT6HYWZXE9K",
    "K2EGYVZ6CY96RZFX7WCJX4GPJP",

};

#endif

// Helpers
const uid cfg_accounts_sz = ARRAYSIZE(cfg_accounts);
const uid cfg_keys_sz = ARRAYSIZE(cfg_keys);