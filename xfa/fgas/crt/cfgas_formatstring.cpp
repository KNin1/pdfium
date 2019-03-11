// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fgas/crt/cfgas_formatstring.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "core/fxcrt/fx_extension.h"
#include "core/fxcrt/fx_safe_types.h"
#include "third_party/base/span.h"
#include "xfa/fgas/crt/cfgas_decimal.h"

#define FX_LOCALECATEGORY_DateHash 0xbde9abde
#define FX_LOCALECATEGORY_TimeHash 0x2d71b00f
#define FX_LOCALECATEGORY_DateTimeHash 0x158c72ed
#define FX_LOCALECATEGORY_NumHash 0x0b4ff870
#define FX_LOCALECATEGORY_TextHash 0x2d08af85
#define FX_LOCALECATEGORY_ZeroHash 0x568cb500
#define FX_LOCALECATEGORY_NullHash 0x052931bb

#define FX_NUMSTYLE_Percent 0x01
#define FX_NUMSTYLE_Exponent 0x02
#define FX_NUMSTYLE_DotVorv 0x04

namespace {

struct LocaleDateTimeSubCategoryWithHash {
  uint32_t uHash;  // Hashed as wide string.
  FX_LOCALEDATETIMESUBCATEGORY eSubCategory;
};

struct LocaleNumberSubCategoryWithHash {
  uint32_t uHash;  // Hashed as wide string.
  FX_LOCALENUMSUBCATEGORY eSubCategory;
};

#undef SUBC
#define SUBC(a, b, c) a, c

const LocaleDateTimeSubCategoryWithHash g_FXLocaleDateTimeSubCatData[] = {
    {SUBC(0x14da2125, "default", FX_LOCALEDATETIMESUBCATEGORY_Default)},
    {SUBC(0x9041d4b0, "short", FX_LOCALEDATETIMESUBCATEGORY_Short)},
    {SUBC(0xa084a381, "medium", FX_LOCALEDATETIMESUBCATEGORY_Medium)},
    {SUBC(0xcdce56b3, "full", FX_LOCALEDATETIMESUBCATEGORY_Full)},
    {SUBC(0xf6b4afb0, "long", FX_LOCALEDATETIMESUBCATEGORY_Long)},
};

const LocaleNumberSubCategoryWithHash g_FXLocaleNumSubCatData[] = {
    {SUBC(0x46f95531, "percent", FX_LOCALENUMPATTERN_Percent)},
    {SUBC(0x4c4e8acb, "currency", FX_LOCALENUMPATTERN_Currency)},
    {SUBC(0x54034c2f, "decimal", FX_LOCALENUMPATTERN_Decimal)},
    {SUBC(0x7568e6ae, "integer", FX_LOCALENUMPATTERN_Integer)},
};

#undef SUBC

struct FX_LOCALETIMEZONEINFO {
  const wchar_t* name;
  int16_t iHour;
  int16_t iMinute;
};

const FX_LOCALETIMEZONEINFO g_FXLocaleTimeZoneData[] = {
    {L"CDT", -5, 0}, {L"CST", -6, 0}, {L"EDT", -4, 0}, {L"EST", -5, 0},
    {L"MDT", -6, 0}, {L"MST", -7, 0}, {L"PDT", -7, 0}, {L"PST", -8, 0},
};

const wchar_t gs_wsTimeSymbols[] = L"hHkKMSFAzZ";
const wchar_t gs_wsDateSymbols[] = L"DJMEeGgYwW";
const wchar_t gs_wsConstChars[] = L",-:/. ";

int32_t ParseTimeZone(pdfium::span<const wchar_t> pStr, FX_TIMEZONE* tz) {
  tz->tzHour = 0;
  tz->tzMinute = 0;
  if (pStr.empty())
    return 0;

  size_t iStart = 1;
  size_t iEnd = iStart + 2;
  while (iStart < pStr.size() && iStart < iEnd)
    tz->tzHour = tz->tzHour * 10 + FXSYS_DecimalCharToInt(pStr[iStart++]);

  if (iStart < pStr.size() && pStr[iStart] == ':')
    iStart++;

  iEnd = iStart + 2;
  while (iStart < pStr.size() && iStart < iEnd)
    tz->tzMinute = tz->tzMinute * 10 + FXSYS_DecimalCharToInt(pStr[iStart++]);

  if (pStr[0] == '-')
    tz->tzHour = -tz->tzHour;

  return iStart;
}

int32_t ConvertHex(int32_t iKeyValue, wchar_t ch) {
  if (FXSYS_IsHexDigit(ch))
    return iKeyValue * 16 + FXSYS_HexCharToInt(ch);
  return iKeyValue;
}

WideString GetLiteralText(pdfium::span<const wchar_t> pStrPattern,
                          size_t* iPattern) {
  WideString wsOutput;
  if (*iPattern >= pStrPattern.size() || pStrPattern[*iPattern] != '\'')
    return wsOutput;

  (*iPattern)++;
  int32_t iQuote = 1;
  while (*iPattern < pStrPattern.size()) {
    if (pStrPattern[*iPattern] == '\'') {
      iQuote++;
      if ((*iPattern + 1 >= pStrPattern.size()) ||
          ((pStrPattern[*iPattern + 1] != '\'') && (iQuote % 2 == 0))) {
        break;
      }
      iQuote++;
      (*iPattern)++;
    } else if (pStrPattern[*iPattern] == '\\' &&
               (*iPattern + 1 < pStrPattern.size()) &&
               pStrPattern[*iPattern + 1] == 'u') {
      int32_t iKeyValue = 0;
      *iPattern += 2;
      for (int32_t i = 0; *iPattern < pStrPattern.size() && i < 4; ++i) {
        wchar_t ch = pStrPattern[(*iPattern)++];
        iKeyValue = ConvertHex(iKeyValue, ch);
      }
      if (iKeyValue != 0)
        wsOutput += static_cast<wchar_t>(iKeyValue & 0x0000FFFF);

      continue;
    }
    wsOutput += pStrPattern[(*iPattern)++];
  }
  return wsOutput;
}

WideString GetLiteralTextReverse(pdfium::span<const wchar_t> pStrPattern,
                                 int32_t* iPattern) {
  WideString wsOutput;
  if (pStrPattern[*iPattern] != '\'')
    return wsOutput;

  (*iPattern)--;
  int32_t iQuote = 1;
  while (*iPattern >= 0) {
    if (pStrPattern[*iPattern] == '\'') {
      iQuote++;
      if (*iPattern - 1 < 0 ||
          ((pStrPattern[*iPattern - 1] != '\'') && (iQuote % 2 == 0))) {
        break;
      }
      iQuote++;
      (*iPattern)--;
    } else if (pStrPattern[*iPattern] == '\\' &&
               pStrPattern[*iPattern + 1] == 'u') {
      (*iPattern)--;
      int32_t iKeyValue = 0;
      int32_t iLen = wsOutput.GetLength();
      int32_t i = 1;
      for (; i < iLen && i < 5; i++) {
        wchar_t ch = wsOutput[i];
        iKeyValue = ConvertHex(iKeyValue, ch);
      }
      if (iKeyValue != 0) {
        wsOutput.Delete(0, i);
        wsOutput = (wchar_t)(iKeyValue & 0x0000FFFF) + wsOutput;
      }
      continue;
    }
    wsOutput = pStrPattern[(*iPattern)--] + wsOutput;
  }
  return wsOutput;
}

bool GetNumericDotIndex(const WideString& wsNum,
                        const WideString& wsDotSymbol,
                        int32_t* iDotIndex) {
  pdfium::span<const wchar_t> spNum = wsNum.AsSpan();
  size_t iLenDot = wsDotSymbol.GetLength();
  for (size_t ccf = 0; ccf < spNum.size(); ++ccf) {
    if (spNum[ccf] == '\'') {
      GetLiteralText(spNum, &ccf);
      continue;
    }
    if (ccf + iLenDot <= spNum.size() &&
        wcsncmp(&spNum[ccf], wsDotSymbol.c_str(), iLenDot) == 0) {
      *iDotIndex = ccf;
      return true;
    }
  }
  auto result = wsNum.Find('.');
  *iDotIndex = result.value_or(spNum.size());
  return result.has_value();
}

bool ExtractCountDigits(pdfium::span<const wchar_t> str,
                        int count,
                        int* cc,
                        uint32_t* value) {
  for (int i = count; i > 0; --i) {
    if (*cc >= pdfium::CollectionSize<int32_t>(str))
      return false;
    if (!FXSYS_IsDecimalDigit(str[*cc]))
      return false;
    *value = *value * 10 + FXSYS_DecimalCharToInt(str[(*cc)++]);
  }
  return true;
}

bool ExtractCountDigitsWithOptional(pdfium::span<const wchar_t> str,
                                    int count,
                                    int* cc,
                                    uint32_t* value) {
  if (!ExtractCountDigits(str, count, cc, value))
    return false;
  ExtractCountDigits(str, 1, cc, value);
  return true;
}

bool ParseLocaleDate(const WideString& wsDate,
                     const WideString& wsDatePattern,
                     LocaleIface* pLocale,
                     CFX_DateTime* datetime,
                     int32_t* cc) {
  uint32_t year = 1900;
  uint32_t month = 1;
  uint32_t day = 1;
  size_t ccf = 0;
  pdfium::span<const wchar_t> spDate = wsDate.AsSpan();
  int32_t len = wsDate.GetLength();
  pdfium::span<const wchar_t> spDatePattern = wsDatePattern.AsSpan();
  WideStringView wsDateSymbols(gs_wsDateSymbols);
  while (*cc < len && ccf < spDatePattern.size()) {
    if (spDatePattern[ccf] == '\'') {
      WideString wsLiteral = GetLiteralText(spDatePattern, &ccf);
      int32_t iLiteralLen = wsLiteral.GetLength();
      if (*cc + iLiteralLen > len ||
          wcsncmp(spDate.data() + *cc, wsLiteral.c_str(), iLiteralLen) != 0) {
        return false;
      }
      *cc += iLiteralLen;
      ccf++;
      continue;
    }
    if (!wsDateSymbols.Contains(spDatePattern[ccf])) {
      if (spDatePattern[ccf] != spDate[*cc])
        return false;
      (*cc)++;
      ccf++;
      continue;
    }

    WideString symbol;
    symbol.Reserve(4);
    symbol += spDatePattern[ccf++];
    while (ccf < spDatePattern.size() && spDatePattern[ccf] == symbol[0]) {
      symbol += spDatePattern[ccf++];
    }
    if (symbol.EqualsASCII("D") || symbol.EqualsASCII("DD")) {
      day = 0;
      if (!ExtractCountDigitsWithOptional(spDate, 1, cc, &day))
        return false;
    } else if (symbol.EqualsASCII("J")) {
      uint32_t val = 0;
      ExtractCountDigits(spDate, 3, cc, &val);
    } else if (symbol.EqualsASCII("M") || symbol.EqualsASCII("MM")) {
      month = 0;
      if (!ExtractCountDigitsWithOptional(spDate, 1, cc, &month))
        return false;
    } else if (symbol.EqualsASCII("MMM") || symbol.EqualsASCII("MMMM")) {
      for (uint16_t i = 0; i < 12; i++) {
        WideString wsMonthName =
            pLocale->GetMonthName(i, symbol.EqualsASCII("MMM"));
        if (wsMonthName.IsEmpty())
          continue;
        if (wcsncmp(wsMonthName.c_str(), spDate.data() + *cc,
                    wsMonthName.GetLength()) == 0) {
          *cc += wsMonthName.GetLength();
          month = i + 1;
          break;
        }
      }
    } else if (symbol.EqualsASCII("EEE") || symbol.EqualsASCII("EEEE")) {
      for (uint16_t i = 0; i < 7; i++) {
        WideString wsDayName =
            pLocale->GetDayName(i, symbol.EqualsASCII("EEE"));
        if (wsDayName.IsEmpty())
          continue;
        if (wcsncmp(wsDayName.c_str(), spDate.data() + *cc,
                    wsDayName.GetLength()) == 0) {
          *cc += wsDayName.GetLength();
          break;
        }
      }
    } else if (symbol.EqualsASCII("YY") || symbol.EqualsASCII("YYYY")) {
      if (*cc + pdfium::base::checked_cast<int32_t>(symbol.GetLength()) > len)
        return false;

      year = 0;
      if (!ExtractCountDigits(spDate, symbol.GetLength(), cc, &year))
        return false;
      if (symbol.EqualsASCII("YY")) {
        if (year <= 29)
          year += 2000;
        else
          year += 1900;
      }
    } else if (symbol.EqualsASCII("G")) {
      *cc += 2;
    } else if (symbol.EqualsASCII("JJJ") || symbol.EqualsASCIINoCase("E") ||
               symbol.EqualsASCII("w") || symbol.EqualsASCII("WW")) {
      *cc += symbol.GetLength();
    }
  }
  if (*cc < len)
    return false;

  datetime->SetDate(year, month, day);
  return !!(*cc);
}

void ResolveZone(FX_TIMEZONE tzDiff,
                 const LocaleIface* pLocale,
                 uint32_t* wHour,
                 uint32_t* wMinute) {
  int32_t iMinuteDiff = *wHour * 60 + *wMinute;
  FX_TIMEZONE tzLocale = pLocale->GetTimeZone();
  iMinuteDiff += tzLocale.tzHour * 60 +
                 (tzLocale.tzHour < 0 ? -tzLocale.tzMinute : tzLocale.tzMinute);
  iMinuteDiff -= tzDiff.tzHour * 60 +
                 (tzDiff.tzHour < 0 ? -tzDiff.tzMinute : tzDiff.tzMinute);

  iMinuteDiff %= 1440;
  if (iMinuteDiff < 0)
    iMinuteDiff += 1440;

  *wHour = iMinuteDiff / 60;
  *wMinute = iMinuteDiff % 60;
}

bool ParseLocaleTime(const WideString& wsTime,
                     const WideString& wsTimePattern,
                     LocaleIface* pLocale,
                     CFX_DateTime* datetime,
                     int32_t* cc) {
  uint32_t hour = 0;
  uint32_t minute = 0;
  uint32_t second = 0;
  uint32_t millisecond = 0;
  size_t ccf = 0;
  pdfium::span<const wchar_t> spTime = wsTime.AsSpan();
  pdfium::span<const wchar_t> spTimePattern = wsTimePattern.AsSpan();
  bool bHasA = false;
  bool bPM = false;
  WideStringView wsTimeSymbols(gs_wsTimeSymbols);
  while (*cc < pdfium::CollectionSize<int32_t>(spTime) &&
         ccf < spTimePattern.size()) {
    if (spTimePattern[ccf] == '\'') {
      WideString wsLiteral = GetLiteralText(spTimePattern, &ccf);
      int32_t iLiteralLen = wsLiteral.GetLength();
      if (*cc + iLiteralLen > pdfium::CollectionSize<int32_t>(spTime) ||
          wcsncmp(spTime.data() + *cc, wsLiteral.c_str(), iLiteralLen)) {
        return false;
      }
      *cc += iLiteralLen;
      ccf++;
      continue;
    }
    if (!wsTimeSymbols.Contains(spTimePattern[ccf])) {
      if (spTimePattern[ccf] != spTime[*cc])
        return false;
      (*cc)++;
      ccf++;
      continue;
    }

    WideString symbol;
    symbol.Reserve(4);
    symbol += spTimePattern[ccf++];
    while (ccf < spTimePattern.size() && spTimePattern[ccf] == symbol[0])
      symbol += spTimePattern[ccf++];

    if (symbol.EqualsASCIINoCase("k") || symbol.EqualsASCIINoCase("h")) {
      hour = 0;
      if (!ExtractCountDigitsWithOptional(spTime, 1, cc, &hour))
        return false;
      if (symbol.EqualsASCII("K") && hour == 24)
        hour = 0;
    } else if (symbol.EqualsASCIINoCase("kk") ||
               symbol.EqualsASCIINoCase("hh")) {
      hour = 0;
      if (!ExtractCountDigits(spTime, 2, cc, &hour))
        return false;
      if (symbol.EqualsASCII("KK") && hour == 24)
        hour = 0;
    } else if (symbol.EqualsASCII("M")) {
      minute = 0;
      if (!ExtractCountDigitsWithOptional(spTime, 1, cc, &minute))
        return false;
    } else if (symbol.EqualsASCII("MM")) {
      minute = 0;
      if (!ExtractCountDigits(spTime, 2, cc, &minute))
        return false;
    } else if (symbol.EqualsASCII("S")) {
      second = 0;
      if (!ExtractCountDigitsWithOptional(spTime, 1, cc, &second))
        return false;
    } else if (symbol.EqualsASCII("SS")) {
      second = 0;
      if (!ExtractCountDigits(spTime, 2, cc, &second))
        return false;
    } else if (symbol.EqualsASCII("FFF")) {
      millisecond = 0;
      if (!ExtractCountDigits(spTime, 3, cc, &millisecond))
        return false;
    } else if (symbol.EqualsASCII("A")) {
      WideString wsAM = pLocale->GetMeridiemName(true);
      WideString wsPM = pLocale->GetMeridiemName(false);
      if (*cc + wsAM.GetLength() <= spTime.size() &&
          WideStringView(spTime.data() + *cc, wsAM.GetLength()) == wsAM) {
        *cc += wsAM.GetLength();
        bHasA = true;
      } else if (*cc + wsPM.GetLength() <= spTime.size() &&
                 WideStringView(spTime.data() + *cc, wsPM.GetLength()) ==
                     wsPM) {
        *cc += wsPM.GetLength();
        bHasA = true;
        bPM = true;
      }
    } else if (symbol.EqualsASCII("Z")) {
      if (*cc + 3 > pdfium::CollectionSize<int32_t>(spTime))
        continue;

      WideString tz(spTime[(*cc)++]);
      tz += spTime[(*cc)++];
      tz += spTime[(*cc)++];
      if (tz.EqualsASCII("GMT")) {
        FX_TIMEZONE tzDiff;
        tzDiff.tzHour = 0;
        tzDiff.tzMinute = 0;
        if (*cc < pdfium::CollectionSize<int32_t>(spTime) &&
            (spTime[*cc] == '-' || spTime[*cc] == '+')) {
          *cc += ParseTimeZone(spTime.subspan(*cc), &tzDiff);
        }
        ResolveZone(tzDiff, pLocale, &hour, &minute);
      } else {
        // Search the timezone list. There are only 8 of them, so linear scan.
        for (size_t i = 0; i < FX_ArraySize(g_FXLocaleTimeZoneData); ++i) {
          const FX_LOCALETIMEZONEINFO& info = g_FXLocaleTimeZoneData[i];
          if (tz != info.name)
            continue;

          hour += info.iHour;
          minute += info.iHour > 0 ? info.iMinute : -info.iMinute;
          break;
        }
      }
    } else if (symbol.EqualsASCII("z")) {
      if (spTime[*cc] != 'Z') {
        FX_TIMEZONE tzDiff;
        *cc += ParseTimeZone(spTime.subspan(*cc), &tzDiff);
        ResolveZone(tzDiff, pLocale, &hour, &minute);
      } else {
        (*cc)++;
      }
    }
  }
  if (bHasA) {
    if (bPM) {
      hour += 12;
      if (hour == 24)
        hour = 12;
    } else {
      if (hour == 12)
        hour = 0;
    }
  }
  datetime->SetTime(hour, minute, second, millisecond);
  return !!(*cc);
}

int32_t GetNumTrailingLimit(const WideString& wsFormat,
                            int iDotPos,
                            bool* bTrimTailZeros) {
  if (iDotPos < 0)
    return 0;

  int32_t iCount = wsFormat.GetLength();
  int32_t iTreading = 0;
  for (iDotPos++; iDotPos < iCount; iDotPos++) {
    wchar_t wc = wsFormat[iDotPos];
    if (wc == L'z' || wc == L'9' || wc == 'Z') {
      iTreading++;
      *bTrimTailZeros = wc != L'9';
    }
  }
  return iTreading;
}

bool IsLeapYear(uint32_t year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

bool MonthHas30Days(uint32_t month) {
  return month == 4 || month == 6 || month == 9 || month == 11;
}

bool MonthHas31Days(uint32_t month) {
  return month != 2 && !MonthHas30Days(month);
}

// |month| is 1-based. e.g. 1 means January.
uint16_t GetSolarMonthDays(uint16_t year, uint16_t month) {
  if (month == 2)
    return FX_IsLeapYear(year) ? 29 : 28;

  return MonthHas30Days(month) ? 30 : 31;
}

uint16_t GetWeekDay(uint16_t year, uint16_t month, uint16_t day) {
  static const uint16_t month_day[] = {0, 3, 3, 6, 1, 4, 6, 2, 5, 0, 3, 5};
  uint16_t nDays =
      (year - 1) % 7 + (year - 1) / 4 - (year - 1) / 100 + (year - 1) / 400;
  nDays += month_day[month - 1] + day;
  if (FX_IsLeapYear(year) && month > 2)
    nDays++;
  return nDays % 7;
}

uint16_t GetWeekOfMonth(uint16_t year, uint16_t month, uint16_t day) {
  uint16_t week_day = GetWeekDay(year, month, 1);
  uint16_t week_index = 0;
  week_index += day / 7;
  day = day % 7;
  if (week_day + day > 7)
    week_index++;
  return week_index;
}

uint16_t GetWeekOfYear(uint16_t year, uint16_t month, uint16_t day) {
  uint16_t nDays = 0;
  for (uint16_t i = 1; i < month; i++)
    nDays += GetSolarMonthDays(year, i);

  nDays += day;
  uint16_t week_day = GetWeekDay(year, 1, 1);
  uint16_t week_index = 1;
  week_index += nDays / 7;
  nDays = nDays % 7;
  if (week_day + nDays > 7)
    week_index++;
  return week_index;
}

WideString NumToString(size_t fmt_size, int32_t value) {
  return WideString::Format(
      fmt_size == 1 ? L"%d" : fmt_size == 2 ? L"%02d" : L"%03d", value);
}

WideString DateFormat(const WideString& wsDatePattern,
                      LocaleIface* pLocale,
                      const CFX_DateTime& datetime) {
  WideString wsResult;
  int32_t year = datetime.GetYear();
  uint8_t month = datetime.GetMonth();
  uint8_t day = datetime.GetDay();
  size_t ccf = 0;
  pdfium::span<const wchar_t> spDatePattern = wsDatePattern.AsSpan();
  WideStringView wsDateSymbols(gs_wsDateSymbols);
  while (ccf < spDatePattern.size()) {
    if (spDatePattern[ccf] == '\'') {
      wsResult += GetLiteralText(spDatePattern, &ccf);
      ccf++;
      continue;
    }
    if (!wsDateSymbols.Contains(spDatePattern[ccf])) {
      wsResult += spDatePattern[ccf++];
      continue;
    }

    WideString symbol;
    symbol.Reserve(4);
    symbol += spDatePattern[ccf++];
    while (ccf < spDatePattern.size() && spDatePattern[ccf] == symbol[0])
      symbol += spDatePattern[ccf++];

    if (symbol.EqualsASCII("D") || symbol.EqualsASCII("DD")) {
      wsResult += NumToString(symbol.GetLength(), day);
    } else if (symbol.EqualsASCII("J") || symbol.EqualsASCII("JJJ")) {
      uint16_t nDays = 0;
      for (int i = 1; i < month; i++)
        nDays += GetSolarMonthDays(year, i);
      nDays += day;
      wsResult += NumToString(symbol.GetLength(), nDays);
    } else if (symbol.EqualsASCII("M") || symbol.EqualsASCII("MM")) {
      wsResult += NumToString(symbol.GetLength(), month);
    } else if (symbol.EqualsASCII("MMM") || symbol.EqualsASCII("MMMM")) {
      wsResult += pLocale->GetMonthName(month - 1, symbol.EqualsASCII("MMM"));
    } else if (symbol.EqualsASCIINoCase("e")) {
      uint16_t wWeekDay = GetWeekDay(year, month, day);
      wsResult +=
          NumToString(1, symbol.EqualsASCII("E") ? wWeekDay + 1
                                                 : (wWeekDay ? wWeekDay : 7));
    } else if (symbol.EqualsASCII("EEE") || symbol.EqualsASCII("EEEE")) {
      wsResult += pLocale->GetDayName(GetWeekDay(year, month, day),
                                      symbol.EqualsASCII("EEE"));
    } else if (symbol.EqualsASCII("G")) {
      wsResult += pLocale->GetEraName(year > 0);
    } else if (symbol.EqualsASCII("YY")) {
      wsResult += NumToString(2, year % 100);
    } else if (symbol.EqualsASCII("YYYY")) {
      wsResult += NumToString(1, year);
    } else if (symbol.EqualsASCII("w")) {
      wsResult += NumToString(1, GetWeekOfMonth(year, month, day));
    } else if (symbol.EqualsASCII("WW")) {
      wsResult += NumToString(2, GetWeekOfYear(year, month, day));
    }
  }
  return wsResult;
}

WideString TimeFormat(const WideString& wsTimePattern,
                      LocaleIface* pLocale,
                      const CFX_DateTime& datetime) {
  WideString wsResult;
  uint8_t hour = datetime.GetHour();
  uint8_t minute = datetime.GetMinute();
  uint8_t second = datetime.GetSecond();
  uint16_t millisecond = datetime.GetMillisecond();
  size_t ccf = 0;
  pdfium::span<const wchar_t> spTimePattern = wsTimePattern.AsSpan();
  uint16_t wHour = hour;
  bool bPM = false;
  if (wsTimePattern.Contains('A')) {
    if (wHour >= 12)
      bPM = true;
  }

  WideStringView wsTimeSymbols(gs_wsTimeSymbols);
  while (ccf < spTimePattern.size()) {
    if (spTimePattern[ccf] == '\'') {
      wsResult += GetLiteralText(spTimePattern, &ccf);
      ccf++;
      continue;
    }
    if (!wsTimeSymbols.Contains(spTimePattern[ccf])) {
      wsResult += spTimePattern[ccf++];
      continue;
    }

    WideString symbol;
    symbol.Reserve(4);
    symbol += spTimePattern[ccf++];
    while (ccf < spTimePattern.size() && spTimePattern[ccf] == symbol[0])
      symbol += spTimePattern[ccf++];

    if (symbol.EqualsASCII("h") || symbol.EqualsASCII("hh")) {
      if (wHour > 12)
        wHour -= 12;
      wsResult += NumToString(symbol.GetLength(), wHour == 0 ? 12 : wHour);
    } else if (symbol.EqualsASCII("K") || symbol.EqualsASCII("KK")) {
      wsResult += NumToString(symbol.GetLength(), wHour == 0 ? 24 : wHour);
    } else if (symbol.EqualsASCII("k") || symbol.EqualsASCII("kk")) {
      if (wHour > 12)
        wHour -= 12;
      wsResult += NumToString(symbol.GetLength(), wHour);
    } else if (symbol.EqualsASCII("H") || symbol.EqualsASCII("HH")) {
      wsResult += NumToString(symbol.GetLength(), wHour);
    } else if (symbol.EqualsASCII("M") || symbol.EqualsASCII("MM")) {
      wsResult += NumToString(symbol.GetLength(), minute);
    } else if (symbol.EqualsASCII("S") || symbol.EqualsASCII("SS")) {
      wsResult += NumToString(symbol.GetLength(), second);
    } else if (symbol.EqualsASCII("FFF")) {
      wsResult += NumToString(3, millisecond);
    } else if (symbol.EqualsASCII("A")) {
      wsResult += pLocale->GetMeridiemName(!bPM);
    } else if (symbol.EqualsASCIINoCase("z")) {
      if (symbol.EqualsASCII("Z"))
        wsResult += L"GMT";
      FX_TIMEZONE tz = pLocale->GetTimeZone();
      if (tz.tzHour != 0 || tz.tzMinute != 0) {
        wsResult += tz.tzHour < 0 ? L"-" : L"+";
        wsResult +=
            WideString::Format(L"%02d:%02d", abs(tz.tzHour), tz.tzMinute);
      }
    }
  }
  return wsResult;
}

WideString FormatDateTimeInternal(const CFX_DateTime& dt,
                                  const WideString& wsDatePattern,
                                  const WideString& wsTimePattern,
                                  bool bDateFirst,
                                  LocaleIface* pLocale) {
  WideString wsDateOut;
  if (!wsDatePattern.IsEmpty())
    wsDateOut = DateFormat(wsDatePattern, pLocale, dt);

  WideString wsTimeOut;
  if (!wsTimePattern.IsEmpty())
    wsTimeOut = TimeFormat(wsTimePattern, pLocale, dt);

  return bDateFirst ? wsDateOut + wsTimeOut : wsTimeOut + wsDateOut;
}

}  // namespace

bool FX_DateFromCanonical(pdfium::span<const wchar_t> spDate,
                          CFX_DateTime* datetime) {
  if (spDate.size() > 10)
    return false;

  int cc = 0;
  uint32_t year = 0;
  if (!ExtractCountDigits(spDate, 4, &cc, &year))
    return false;
  if (year < 1900)
    return false;
  if (cc >= pdfium::CollectionSize<int32_t>(spDate)) {
    datetime->SetDate(year, 1, 1);
    return true;
  }

  if (spDate[cc] == '-')
    cc++;

  uint32_t month = 0;
  if (!ExtractCountDigits(spDate, 2, &cc, &month))
    return false;
  if (month > 12 || month < 1)
    return false;
  if (cc >= pdfium::CollectionSize<int32_t>(spDate)) {
    datetime->SetDate(year, month, 1);
    return true;
  }

  if (spDate[cc] == '-')
    cc++;

  uint32_t day = 0;
  if (!ExtractCountDigits(spDate, 2, &cc, &day))
    return false;
  if (day < 1)
    return false;
  if ((MonthHas31Days(month) && day > 31) ||
      (MonthHas30Days(month) && day > 30)) {
    return false;
  }
  if (month == 2 && day > (IsLeapYear(year) ? 29U : 28U))
    return false;

  datetime->SetDate(year, month, day);
  return true;
}

bool FX_TimeFromCanonical(const LocaleIface* pLocale,
                          pdfium::span<const wchar_t> spTime,
                          CFX_DateTime* datetime) {
  if (spTime.empty())
    return false;

  int cc = 0;
  uint32_t hour = 0;
  if (!ExtractCountDigits(spTime, 2, &cc, &hour) || hour >= 24)
    return false;

  if (cc >= pdfium::CollectionSize<int32_t>(spTime)) {
    datetime->SetTime(hour, 0, 0, 0);
    return true;
  }

  if (spTime[cc] == ':')
    cc++;

  uint32_t minute = 0;
  if (!ExtractCountDigits(spTime, 2, &cc, &minute))
    return false;
  if (minute >= 60)
    return false;

  if (cc >= pdfium::CollectionSize<int32_t>(spTime)) {
    datetime->SetTime(hour, minute, 0, 0);
    return true;
  }

  if (spTime[cc] == ':')
    cc++;

  uint32_t second = 0;
  uint32_t millisecond = 0;
  if (spTime[cc] != 'Z') {
    if (!ExtractCountDigits(spTime, 2, &cc, &second) || second >= 60)
      return false;

    if (cc < pdfium::CollectionSize<int32_t>(spTime) && spTime[cc] == '.') {
      cc++;
      if (!ExtractCountDigits(spTime, 3, &cc, &millisecond))
        return false;
    }
  }

  // Skip until we find a + or - for the time zone.
  while (cc < pdfium::CollectionSize<int32_t>(spTime)) {
    if (spTime[cc] == '+' || spTime[cc] == '-')
      break;
    ++cc;
  }

  if (cc < pdfium::CollectionSize<int32_t>(spTime)) {
    FX_TIMEZONE tzDiff;
    tzDiff.tzHour = 0;
    tzDiff.tzMinute = 0;
    if (spTime[cc] != 'Z')
      cc += ParseTimeZone(spTime.subspan(cc), &tzDiff);
    ResolveZone(tzDiff, pLocale, &hour, &minute);
  }

  datetime->SetTime(hour, minute, second, millisecond);
  return true;
}

CFGAS_FormatString::CFGAS_FormatString(LocaleMgrIface* pLocaleMgr)
    : m_pLocaleMgr(pLocaleMgr) {}

CFGAS_FormatString::~CFGAS_FormatString() {}

void CFGAS_FormatString::SplitFormatString(
    const WideString& wsFormatString,
    std::vector<WideString>* wsPatterns) const {
  int32_t iStrLen = wsFormatString.GetLength();
  const wchar_t* pStr = wsFormatString.c_str();
  const wchar_t* pToken = pStr;
  const wchar_t* pEnd = pStr + iStrLen;
  bool bQuote = false;
  while (true) {
    if (pStr >= pEnd) {
      wsPatterns->push_back(WideString(pToken, pStr - pToken));
      return;
    }
    if (*pStr == '\'') {
      bQuote = !bQuote;
    } else if (*pStr == L'|' && !bQuote) {
      wsPatterns->push_back(WideString(pToken, pStr - pToken));
      pToken = pStr + 1;
    }
    pStr++;
  }
}

FX_LOCALECATEGORY CFGAS_FormatString::GetCategory(
    const WideString& wsPattern) const {
  FX_LOCALECATEGORY eCategory = FX_LOCALECATEGORY_Unknown;
  pdfium::span<const wchar_t> spPattern = wsPattern.AsSpan();
  size_t ccf = 0;
  bool bBraceOpen = false;
  WideStringView wsConstChars(gs_wsConstChars);
  while (ccf < spPattern.size()) {
    if (spPattern[ccf] == '\'') {
      GetLiteralText(spPattern, &ccf);
    } else if (!bBraceOpen && !wsConstChars.Contains(spPattern[ccf])) {
      WideString wsCategory(spPattern[ccf]);
      ccf++;
      while (true) {
        if (ccf == spPattern.size())
          return eCategory;
        if (spPattern[ccf] == '.' || spPattern[ccf] == '(')
          break;
        if (spPattern[ccf] == '{') {
          bBraceOpen = true;
          break;
        }
        wsCategory += spPattern[ccf];
        ccf++;
      }

      uint32_t dwHash = FX_HashCode_GetW(wsCategory.AsStringView(), false);
      if (dwHash == FX_LOCALECATEGORY_DateTimeHash)
        return FX_LOCALECATEGORY_DateTime;
      if (dwHash == FX_LOCALECATEGORY_TextHash)
        return FX_LOCALECATEGORY_Text;
      if (dwHash == FX_LOCALECATEGORY_NumHash)
        return FX_LOCALECATEGORY_Num;
      if (dwHash == FX_LOCALECATEGORY_ZeroHash)
        return FX_LOCALECATEGORY_Zero;
      if (dwHash == FX_LOCALECATEGORY_NullHash)
        return FX_LOCALECATEGORY_Null;
      if (dwHash == FX_LOCALECATEGORY_DateHash) {
        if (eCategory == FX_LOCALECATEGORY_Time)
          return FX_LOCALECATEGORY_DateTime;
        eCategory = FX_LOCALECATEGORY_Date;
      } else if (dwHash == FX_LOCALECATEGORY_TimeHash) {
        if (eCategory == FX_LOCALECATEGORY_Date)
          return FX_LOCALECATEGORY_DateTime;
        eCategory = FX_LOCALECATEGORY_Time;
      }
    } else if (spPattern[ccf] == '}') {
      bBraceOpen = false;
    }
    ccf++;
  }
  return eCategory;
}

WideString CFGAS_FormatString::GetTextFormat(const WideString& wsPattern,
                                             WideStringView wsCategory) const {
  size_t ccf = 0;
  pdfium::span<const wchar_t> spPattern = wsPattern.AsSpan();
  bool bBrackOpen = false;
  WideStringView wsConstChars(gs_wsConstChars);
  WideString wsPurgePattern;
  while (ccf < spPattern.size()) {
    if (spPattern[ccf] == '\'') {
      int32_t iCurChar = ccf;
      GetLiteralText(spPattern, &ccf);
      wsPurgePattern +=
          WideStringView(spPattern.data() + iCurChar, ccf - iCurChar + 1);
    } else if (!bBrackOpen && !wsConstChars.Contains(spPattern[ccf])) {
      WideString wsSearchCategory(spPattern[ccf]);
      ccf++;
      while (ccf < spPattern.size() && spPattern[ccf] != '{' &&
             spPattern[ccf] != '.' && spPattern[ccf] != '(') {
        wsSearchCategory += spPattern[ccf];
        ccf++;
      }
      if (wsSearchCategory != wsCategory)
        continue;

      while (ccf < spPattern.size()) {
        if (spPattern[ccf] == '(') {
          ccf++;
          // Skip over the encoding name.
          while (ccf < spPattern.size() && spPattern[ccf] != ')')
            ccf++;
        } else if (spPattern[ccf] == '{') {
          bBrackOpen = true;
          break;
        }
        ccf++;
      }
    } else if (spPattern[ccf] != '}') {
      wsPurgePattern += spPattern[ccf];
    }
    ccf++;
  }
  if (!bBrackOpen)
    wsPurgePattern = wsPattern;

  return wsPurgePattern;
}

LocaleIface* CFGAS_FormatString::GetNumericFormat(
    const WideString& wsPattern,
    int32_t* iDotIndex,
    uint32_t* dwStyle,
    WideString* wsPurgePattern) const {
  *dwStyle = 0;
  LocaleIface* pLocale = nullptr;
  size_t ccf = 0;
  pdfium::span<const wchar_t> spPattern = wsPattern.AsSpan();
  bool bFindDot = false;
  bool bBrackOpen = false;
  WideStringView wsConstChars(gs_wsConstChars);
  while (ccf < spPattern.size()) {
    if (spPattern[ccf] == '\'') {
      int32_t iCurChar = ccf;
      GetLiteralText(spPattern, &ccf);
      *wsPurgePattern +=
          WideStringView(spPattern.data() + iCurChar, ccf - iCurChar + 1);
    } else if (!bBrackOpen && !wsConstChars.Contains(spPattern[ccf])) {
      WideString wsCategory(spPattern[ccf]);
      ccf++;
      while (ccf < spPattern.size() && spPattern[ccf] != '{' &&
             spPattern[ccf] != '.' && spPattern[ccf] != '(') {
        wsCategory += spPattern[ccf];
        ccf++;
      }
      if (!wsCategory.EqualsASCII("num")) {
        bBrackOpen = true;
        ccf = 0;
        continue;
      }
      while (ccf < spPattern.size()) {
        if (spPattern[ccf] == '{') {
          bBrackOpen = true;
          break;
        }
        if (spPattern[ccf] == '(') {
          ccf++;
          WideString wsLCID;
          while (ccf < spPattern.size() && spPattern[ccf] != ')')
            wsLCID += spPattern[ccf++];

          pLocale = m_pLocaleMgr->GetLocaleByName(wsLCID);
        } else if (spPattern[ccf] == '.') {
          WideString wsSubCategory;
          ccf++;
          while (ccf < spPattern.size() && spPattern[ccf] != '(' &&
                 spPattern[ccf] != '{') {
            wsSubCategory += spPattern[ccf++];
          }
          uint32_t dwSubHash =
              FX_HashCode_GetW(wsSubCategory.AsStringView(), false);
          FX_LOCALENUMSUBCATEGORY eSubCategory = FX_LOCALENUMPATTERN_Decimal;
          for (const auto& data : g_FXLocaleNumSubCatData) {
            if (data.uHash == dwSubHash) {
              eSubCategory = data.eSubCategory;
              break;
            }
          }
          if (!pLocale)
            pLocale = m_pLocaleMgr->GetDefLocale();

          ASSERT(pLocale);

          wsSubCategory = pLocale->GetNumPattern(eSubCategory);
          auto result = wsSubCategory.Find('.');
          if (result.has_value() && result.value() != 0) {
            if (!bFindDot)
              *iDotIndex = wsPurgePattern->GetLength() - 1;
            bFindDot = true;
            *dwStyle |= FX_NUMSTYLE_DotVorv;
          }
          *wsPurgePattern += wsSubCategory;
          if (eSubCategory == FX_LOCALENUMPATTERN_Percent)
            *dwStyle |= FX_NUMSTYLE_Percent;

          continue;
        }
        ccf++;
      }
    } else if (spPattern[ccf] == 'E') {
      *dwStyle |= FX_NUMSTYLE_Exponent;
      *wsPurgePattern += spPattern[ccf];
    } else if (spPattern[ccf] == '%') {
      *dwStyle |= FX_NUMSTYLE_Percent;
      *wsPurgePattern += spPattern[ccf];
    } else if (spPattern[ccf] != '}') {
      *wsPurgePattern += spPattern[ccf];
    }
    if (!bFindDot && ccf < spPattern.size() &&
        (spPattern[ccf] == '.' || spPattern[ccf] == 'V' ||
         spPattern[ccf] == 'v')) {
      bFindDot = true;
      *iDotIndex = wsPurgePattern->GetLength() - 1;
      *dwStyle |= FX_NUMSTYLE_DotVorv;
    }
    ccf++;
  }
  if (!bFindDot)
    *iDotIndex = wsPurgePattern->GetLength();
  if (!pLocale)
    pLocale = m_pLocaleMgr->GetDefLocale();
  return pLocale;
}

bool CFGAS_FormatString::ParseText(const WideString& wsSrcText,
                                   const WideString& wsPattern,
                                   WideString* wsValue) const {
  wsValue->clear();
  if (wsSrcText.IsEmpty() || wsPattern.IsEmpty())
    return false;

  WideString wsTextFormat = GetTextFormat(wsPattern, L"text");
  if (wsTextFormat.IsEmpty())
    return false;

  int32_t iText = 0;
  size_t iPattern = 0;
  const wchar_t* pStrText = wsSrcText.c_str();
  int32_t iLenText = wsSrcText.GetLength();
  pdfium::span<const wchar_t> spTextFormat = wsTextFormat.AsSpan();
  while (iPattern < spTextFormat.size() && iText < iLenText) {
    switch (spTextFormat[iPattern]) {
      case '\'': {
        WideString wsLiteral = GetLiteralText(spTextFormat, &iPattern);
        int32_t iLiteralLen = wsLiteral.GetLength();
        if (iText + iLiteralLen > iLenText ||
            wcsncmp(pStrText + iText, wsLiteral.c_str(), iLiteralLen)) {
          *wsValue = wsSrcText;
          return false;
        }
        iText += iLiteralLen;
        iPattern++;
        break;
      }
      case 'A':
        if (FXSYS_iswalpha(pStrText[iText])) {
          *wsValue += pStrText[iText];
          iText++;
        }
        iPattern++;
        break;
      case 'X':
        *wsValue += pStrText[iText];
        iText++;
        iPattern++;
        break;
      case 'O':
      case '0':
        if (FXSYS_IsDecimalDigit(pStrText[iText]) ||
            FXSYS_iswalpha(pStrText[iText])) {
          *wsValue += pStrText[iText];
          iText++;
        }
        iPattern++;
        break;
      case '9':
        if (FXSYS_IsDecimalDigit(pStrText[iText])) {
          *wsValue += pStrText[iText];
          iText++;
        }
        iPattern++;
        break;
      default:
        if (spTextFormat[iPattern] != pStrText[iText]) {
          *wsValue = wsSrcText;
          return false;
        }
        iPattern++;
        iText++;
        break;
    }
  }
  return iPattern == spTextFormat.size() && iText == iLenText;
}

bool CFGAS_FormatString::ParseNum(const WideString& wsSrcNum,
                                  const WideString& wsPattern,
                                  WideString* wsValue) const {
  wsValue->clear();
  if (wsSrcNum.IsEmpty() || wsPattern.IsEmpty())
    return false;

  int32_t dot_index_f = -1;
  uint32_t dwFormatStyle = 0;
  WideString wsNumFormat;
  LocaleIface* pLocale =
      GetNumericFormat(wsPattern, &dot_index_f, &dwFormatStyle, &wsNumFormat);
  if (!pLocale || wsNumFormat.IsEmpty())
    return false;

  int32_t iExponent = 0;
  WideString wsDotSymbol = pLocale->GetDecimalSymbol();
  WideString wsGroupSymbol = pLocale->GetGroupingSymbol();
  int32_t iGroupLen = wsGroupSymbol.GetLength();
  WideString wsMinus = pLocale->GetMinusSymbol();
  int32_t iMinusLen = wsMinus.GetLength();
  const wchar_t* str = wsSrcNum.c_str();
  int len = wsSrcNum.GetLength();

  pdfium::span<const wchar_t> spNumFormat = wsNumFormat.AsSpan();
  bool bHavePercentSymbol = false;
  bool bNeg = false;
  bool bReverseParse = false;
  int32_t dot_index = 0;

  // If we're looking for a '.', 'V' or 'v' and the input string does not
  // have a dot index for one of those, then we disable parsing the decimal.
  if (!GetNumericDotIndex(wsSrcNum, wsDotSymbol, &dot_index) &&
      (dwFormatStyle & FX_NUMSTYLE_DotVorv))
    bReverseParse = true;

  // This parse is broken into two parts based on the '.' in the number
  // (or 'V' or 'v'). |dot_index_f| is the location of the dot in the format and
  // |dot_index| is the location of the dot in the number.
  //
  // This first while() starts at the '.' and walks backwards to the start of
  // the number. The second while() walks from the dot forwards to the end of
  // the decimal.

  // Use ints for the moment for backwards indexing.
  int cc = dot_index - 1;
  for (int ccf = dot_index_f - 1; ccf >= 0 && cc >= 0;) {
    switch (spNumFormat[ccf]) {
      case '\'': {
        WideString wsLiteral = GetLiteralTextReverse(spNumFormat, &ccf);
        int32_t iLiteralLen = wsLiteral.GetLength();
        cc -= iLiteralLen - 1;
        if (cc < 0 || wcsncmp(str + cc, wsLiteral.c_str(), iLiteralLen))
          return false;

        cc--;
        ccf--;
        break;
      }
      case '9':
        if (!FXSYS_IsDecimalDigit(str[cc]))
          return false;

        wsValue->InsertAtFront(str[cc]);
        cc--;
        ccf--;
        break;
      case 'z':
      case 'Z':
        if (spNumFormat[ccf] == 'z' || str[cc] != ' ') {
          if (FXSYS_IsDecimalDigit(str[cc])) {
            wsValue->InsertAtFront(str[cc]);
            cc--;
          }
        } else {
          cc--;
        }
        ccf--;
        break;
      case 'S':
      case 's':
        if (str[cc] == '+' || (spNumFormat[ccf] == 'S' && str[cc] == ' ')) {
          cc--;
        } else {
          cc -= iMinusLen - 1;
          if (cc < 0 || wcsncmp(str + cc, wsMinus.c_str(), iMinusLen))
            return false;

          cc--;
          bNeg = true;
        }
        ccf--;
        break;
      case 'E': {
        bool bExpSign = false;
        while (cc >= 0) {
          if (str[cc] == 'E' || str[cc] == 'e')
            break;
          if (FXSYS_IsDecimalDigit(str[cc])) {
            iExponent = iExponent + FXSYS_DecimalCharToInt(str[cc]) * 10;
            cc--;
            continue;
          }
          if (str[cc] == '+') {
            cc--;
            continue;
          }
          if (cc - iMinusLen + 1 > 0 && !wcsncmp(str + (cc - iMinusLen + 1),
                                                 wsMinus.c_str(), iMinusLen)) {
            bExpSign = true;
            cc -= iMinusLen;
            continue;
          }

          return false;
        }
        cc--;
        iExponent = bExpSign ? -iExponent : iExponent;
        ccf--;
        break;
      }
      case '$': {
        WideString wsSymbol = pLocale->GetCurrencySymbol();
        int32_t iSymbolLen = wsSymbol.GetLength();
        cc -= iSymbolLen - 1;
        if (cc < 0 || wcsncmp(str + cc, wsSymbol.c_str(), iSymbolLen))
          return false;

        cc--;
        ccf--;
        break;
      }
      case 'r':
      case 'R':
        if (ccf - 1 >= 0 &&
            ((spNumFormat[ccf] == 'R' && spNumFormat[ccf - 1] == 'C') ||
             (spNumFormat[ccf] == 'r' && spNumFormat[ccf - 1] == 'c'))) {
          if (spNumFormat[ccf] == 'R' && str[cc] == ' ') {
            cc -= 2;
          } else if (str[cc] == 'R' && cc - 1 >= 0 && str[cc - 1] == 'C') {
            bNeg = true;
            cc -= 2;
          }
          ccf -= 2;
        } else {
          ccf--;
        }
        break;
      case 'b':
      case 'B':
        if (ccf - 1 >= 0 &&
            ((spNumFormat[ccf] == 'B' && spNumFormat[ccf - 1] == 'D') ||
             (spNumFormat[ccf] == 'b' && spNumFormat[ccf - 1] == 'd'))) {
          if (spNumFormat[ccf] == 'B' && str[cc] == ' ') {
            cc -= 2;
          } else if (str[cc] == 'B' && cc - 1 >= 0 && str[cc - 1] == 'D') {
            bNeg = true;
            cc -= 2;
          }
          ccf -= 2;
        } else {
          ccf--;
        }
        break;
      case '%': {
        WideString wsSymbol = pLocale->GetPercentSymbol();
        int32_t iSysmbolLen = wsSymbol.GetLength();
        cc -= iSysmbolLen - 1;
        if (cc < 0 || wcsncmp(str + cc, wsSymbol.c_str(), iSysmbolLen))
          return false;

        cc--;
        ccf--;
        bHavePercentSymbol = true;
        break;
      }
      case '.':
      case 'V':
      case 'v':
      case '8':
        return false;
      case ',': {
        if (cc >= 0) {
          cc -= iGroupLen - 1;
          if (cc >= 0 &&
              wcsncmp(str + cc, wsGroupSymbol.c_str(), iGroupLen) == 0) {
            cc--;
          } else {
            cc += iGroupLen - 1;
          }
        }
        ccf--;
        break;
      }
      case '(':
      case ')':
        if (str[cc] == spNumFormat[ccf])
          bNeg = true;
        else if (str[cc] != L' ')
          return false;

        cc--;
        ccf--;
        break;
      default:
        if (spNumFormat[ccf] != str[cc])
          return false;

        cc--;
        ccf--;
    }
  }
  if (cc >= 0) {
    if (str[cc] == '-') {
      bNeg = true;
      cc--;
    }
    if (cc >= 0)
      return false;
  }
  if (dot_index < len && (dwFormatStyle & FX_NUMSTYLE_DotVorv))
    *wsValue += '.';
  if (!bReverseParse) {
    cc = (dot_index == len) ? len : dot_index + 1;
    for (size_t ccf = dot_index_f + 1; cc < len && ccf < spNumFormat.size();
         ++ccf) {
      switch (spNumFormat[ccf]) {
        case '\'': {
          WideString wsLiteral = GetLiteralText(spNumFormat, &ccf);
          int32_t iLiteralLen = wsLiteral.GetLength();
          if (cc + iLiteralLen > len ||
              wcsncmp(str + cc, wsLiteral.c_str(), iLiteralLen)) {
            return false;
          }
          cc += iLiteralLen;
          break;
        }
        case '9':
          if (!FXSYS_IsDecimalDigit(str[cc]))
            return false;

          *wsValue += str[cc];
          cc++;
          break;
        case 'z':
        case 'Z':
          if (spNumFormat[ccf] == 'z' || str[cc] != ' ') {
            if (FXSYS_IsDecimalDigit(str[cc])) {
              *wsValue += str[cc];
              cc++;
            }
          } else {
            cc++;
          }
          break;
        case 'S':
        case 's':
          if (str[cc] == '+' || (spNumFormat[ccf] == 'S' && str[cc] == ' ')) {
            cc++;
          } else {
            if (cc + iMinusLen > len ||
                wcsncmp(str + cc, wsMinus.c_str(), iMinusLen)) {
              return false;
            }
            bNeg = true;
            cc += iMinusLen;
          }
          break;
        case 'E': {
          if (cc >= len || (str[cc] != 'E' && str[cc] != 'e'))
            return false;

          bool bExpSign = false;
          cc++;
          if (cc < len) {
            if (str[cc] == '+') {
              cc++;
            } else if (str[cc] == '-') {
              bExpSign = true;
              cc++;
            }
          }
          while (cc < len) {
            if (!FXSYS_IsDecimalDigit(str[cc]))
              break;

            iExponent = iExponent * 10 + FXSYS_DecimalCharToInt(str[cc]);
            cc++;
          }
          iExponent = bExpSign ? -iExponent : iExponent;
          break;
        }
        case '$': {
          WideString wsSymbol = pLocale->GetCurrencySymbol();
          int32_t iSymbolLen = wsSymbol.GetLength();
          if (cc + iSymbolLen > len ||
              wcsncmp(str + cc, wsSymbol.c_str(), iSymbolLen)) {
            return false;
          }
          cc += iSymbolLen;
          break;
        }
        case 'c':
        case 'C':
          if (ccf + 1 < spNumFormat.size() &&
              ((spNumFormat[ccf] == 'C' && spNumFormat[ccf + 1] == 'R') ||
               (spNumFormat[ccf] == 'c' && spNumFormat[ccf + 1] == 'r'))) {
            if (spNumFormat[ccf] == 'C' && str[cc] == ' ') {
              cc++;
            } else if (str[cc] == 'C' && cc + 1 < len && str[cc + 1] == 'R') {
              bNeg = true;
              cc += 2;
            }
            ccf++;
          }
          break;
        case 'd':
        case 'D':
          if (ccf + 1 < spNumFormat.size() &&
              ((spNumFormat[ccf] == 'D' && spNumFormat[ccf + 1] == 'B') ||
               (spNumFormat[ccf] == 'd' && spNumFormat[ccf + 1] == 'b'))) {
            if (spNumFormat[ccf] == 'D' && str[cc] == ' ') {
              cc++;
            } else if (str[cc] == 'D' && cc + 1 < len && str[cc + 1] == 'B') {
              bNeg = true;
              cc += 2;
            }
            ccf++;
          }
          break;
        case '.':
        case 'V':
        case 'v':
          return false;
        case '%': {
          WideString wsSymbol = pLocale->GetPercentSymbol();
          int32_t iSysmbolLen = wsSymbol.GetLength();
          if (cc + iSysmbolLen <= len &&
              !wcsncmp(str + cc, wsSymbol.c_str(), iSysmbolLen)) {
            cc += iSysmbolLen;
          }
          bHavePercentSymbol = true;
        } break;
        case '8': {
          while (ccf + 1 < spNumFormat.size() && spNumFormat[ccf + 1] == '8')
            ccf++;

          while (cc < len && FXSYS_IsDecimalDigit(str[cc])) {
            *wsValue += str[cc];
            cc++;
          }
        } break;
        case ',': {
          if (cc + iGroupLen <= len &&
              wcsncmp(str + cc, wsGroupSymbol.c_str(), iGroupLen) == 0) {
            cc += iGroupLen;
          }
          break;
        }
        case '(':
        case ')':
          if (str[cc] == spNumFormat[ccf])
            bNeg = true;
          else if (str[cc] != L' ')
            return false;

          cc++;
          break;
        default:
          if (spNumFormat[ccf] != str[cc])
            return false;

          cc++;
      }
    }
    if (cc != len)
      return false;
  }
  if (iExponent || bHavePercentSymbol) {
    CFGAS_Decimal decimal = CFGAS_Decimal(wsValue->AsStringView());
    if (iExponent) {
      decimal = decimal *
                CFGAS_Decimal(FXSYS_pow(10, static_cast<float>(iExponent)), 3);
    }
    if (bHavePercentSymbol)
      decimal = decimal / CFGAS_Decimal(100);

    *wsValue = decimal.ToWideString();
  }
  if (bNeg)
    wsValue->InsertAtFront(L'-');

  return true;
}

FX_DATETIMETYPE CFGAS_FormatString::GetDateTimeFormat(
    const WideString& wsPattern,
    LocaleIface** pLocale,
    WideString* wsDatePattern,
    WideString* wsTimePattern) const {
  *pLocale = nullptr;
  WideString wsTempPattern;
  FX_LOCALECATEGORY eCategory = FX_LOCALECATEGORY_Unknown;
  size_t ccf = 0;
  pdfium::span<const wchar_t> spPattern = wsPattern.AsSpan();
  int32_t iFindCategory = 0;
  bool bBraceOpen = false;
  WideStringView wsConstChars(gs_wsConstChars);
  while (ccf < spPattern.size()) {
    if (spPattern[ccf] == '\'') {
      int32_t iCurChar = ccf;
      GetLiteralText(spPattern, &ccf);
      wsTempPattern +=
          WideStringView(spPattern.data() + iCurChar, ccf - iCurChar + 1);
    } else if (!bBraceOpen && iFindCategory != 3 &&
               !wsConstChars.Contains(spPattern[ccf])) {
      WideString wsCategory(spPattern[ccf]);
      ccf++;
      while (ccf < spPattern.size() && spPattern[ccf] != '{' &&
             spPattern[ccf] != '.' && spPattern[ccf] != '(') {
        if (spPattern[ccf] == 'T') {
          *wsDatePattern = wsPattern.Left(ccf);
          *wsTimePattern = wsPattern.Right(wsPattern.GetLength() - ccf);
          wsTimePattern->SetAt(0, ' ');
          if (!*pLocale)
            *pLocale = m_pLocaleMgr->GetDefLocale();

          return FX_DATETIMETYPE_DateTime;
        }
        wsCategory += spPattern[ccf];
        ccf++;
      }
      if (!(iFindCategory & 1) && wsCategory.EqualsASCII("date")) {
        iFindCategory |= 1;
        eCategory = FX_LOCALECATEGORY_Date;
        if (iFindCategory & 2)
          iFindCategory = 4;
      } else if (!(iFindCategory & 2) && wsCategory.EqualsASCII("time")) {
        iFindCategory |= 2;
        eCategory = FX_LOCALECATEGORY_Time;
      } else if (wsCategory.EqualsASCII("datetime")) {
        iFindCategory = 3;
        eCategory = FX_LOCALECATEGORY_DateTime;
      } else {
        continue;
      }
      while (ccf < spPattern.size()) {
        if (spPattern[ccf] == '{') {
          bBraceOpen = true;
          break;
        }
        if (spPattern[ccf] == '(') {
          ccf++;
          WideString wsLCID;
          while (ccf < spPattern.size() && spPattern[ccf] != ')')
            wsLCID += spPattern[ccf++];

          *pLocale = m_pLocaleMgr->GetLocaleByName(wsLCID);
        } else if (spPattern[ccf] == '.') {
          WideString wsSubCategory;
          ccf++;
          while (ccf < spPattern.size() && spPattern[ccf] != '(' &&
                 spPattern[ccf] != '{')
            wsSubCategory += spPattern[ccf++];

          uint32_t dwSubHash =
              FX_HashCode_GetW(wsSubCategory.AsStringView(), false);
          FX_LOCALEDATETIMESUBCATEGORY eSubCategory =
              FX_LOCALEDATETIMESUBCATEGORY_Medium;
          for (const auto& data : g_FXLocaleDateTimeSubCatData) {
            if (data.uHash == dwSubHash) {
              eSubCategory = data.eSubCategory;
              break;
            }
          }
          if (!*pLocale)
            *pLocale = m_pLocaleMgr->GetDefLocale();
          ASSERT(*pLocale);

          switch (eCategory) {
            case FX_LOCALECATEGORY_Date:
              *wsDatePattern =
                  wsTempPattern + (*pLocale)->GetDatePattern(eSubCategory);
              break;
            case FX_LOCALECATEGORY_Time:
              *wsTimePattern =
                  wsTempPattern + (*pLocale)->GetTimePattern(eSubCategory);
              break;
            case FX_LOCALECATEGORY_DateTime:
              *wsDatePattern =
                  wsTempPattern + (*pLocale)->GetDatePattern(eSubCategory);
              *wsTimePattern = (*pLocale)->GetTimePattern(eSubCategory);
              break;
            default:
              break;
          }
          wsTempPattern.clear();
          continue;
        }
        ccf++;
      }
    } else if (spPattern[ccf] == '}') {
      bBraceOpen = false;
      if (!wsTempPattern.IsEmpty()) {
        if (eCategory == FX_LOCALECATEGORY_Time)
          *wsTimePattern = std::move(wsTempPattern);
        else if (eCategory == FX_LOCALECATEGORY_Date)
          *wsDatePattern = std::move(wsTempPattern);
        else
          wsTempPattern.clear();
      }
    } else {
      wsTempPattern += spPattern[ccf];
    }
    ccf++;
  }

  if (!wsTempPattern.IsEmpty()) {
    if (eCategory == FX_LOCALECATEGORY_Date)
      *wsDatePattern += wsTempPattern;
    else
      *wsTimePattern += wsTempPattern;
  }
  if (!*pLocale)
    *pLocale = m_pLocaleMgr->GetDefLocale();
  if (!iFindCategory) {
    wsTimePattern->clear();
    *wsDatePattern = wsPattern;
  }
  return (FX_DATETIMETYPE)iFindCategory;
}

bool CFGAS_FormatString::ParseDateTime(const WideString& wsSrcDateTime,
                                       const WideString& wsPattern,
                                       FX_DATETIMETYPE eDateTimeType,
                                       CFX_DateTime* dtValue) const {
  dtValue->Reset();
  if (wsSrcDateTime.IsEmpty() || wsPattern.IsEmpty())
    return false;

  WideString wsDatePattern;
  WideString wsTimePattern;
  LocaleIface* pLocale = nullptr;
  FX_DATETIMETYPE eCategory =
      GetDateTimeFormat(wsPattern, &pLocale, &wsDatePattern, &wsTimePattern);
  if (!pLocale)
    return false;
  if (eCategory == FX_DATETIMETYPE_Unknown)
    eCategory = eDateTimeType;
  if (eCategory == FX_DATETIMETYPE_Unknown)
    return false;
  if (eCategory == FX_DATETIMETYPE_TimeDate) {
    int32_t iStart = 0;
    if (!ParseLocaleTime(wsSrcDateTime, wsTimePattern, pLocale, dtValue,
                         &iStart)) {
      return false;
    }
    if (!ParseLocaleDate(wsSrcDateTime, wsDatePattern, pLocale, dtValue,
                         &iStart)) {
      return false;
    }
  } else {
    int32_t iStart = 0;
    if ((eCategory & FX_DATETIMETYPE_Date) &&
        !ParseLocaleDate(wsSrcDateTime, wsDatePattern, pLocale, dtValue,
                         &iStart)) {
      return false;
    }
    if ((eCategory & FX_DATETIMETYPE_Time) &&
        !ParseLocaleTime(wsSrcDateTime, wsTimePattern, pLocale, dtValue,
                         &iStart)) {
      return false;
    }
  }
  return true;
}

bool CFGAS_FormatString::ParseZero(const WideString& wsSrcText,
                                   const WideString& wsPattern) const {
  WideString wsTextFormat = GetTextFormat(wsPattern, L"zero");

  int32_t iText = 0;
  size_t iPattern = 0;
  const wchar_t* pStrText = wsSrcText.c_str();
  int32_t iLenText = wsSrcText.GetLength();
  pdfium::span<const wchar_t> spTextFormat = wsTextFormat.AsSpan();
  while (iPattern < spTextFormat.size() && iText < iLenText) {
    if (spTextFormat[iPattern] == '\'') {
      WideString wsLiteral = GetLiteralText(spTextFormat, &iPattern);
      int32_t iLiteralLen = wsLiteral.GetLength();
      if (iText + iLiteralLen > iLenText ||
          wcsncmp(pStrText + iText, wsLiteral.c_str(), iLiteralLen)) {
        return false;
      }
      iText += iLiteralLen;
      iPattern++;
      continue;
    }
    if (spTextFormat[iPattern] != pStrText[iText])
      return false;

    iText++;
    iPattern++;
  }
  return iPattern == spTextFormat.size() && iText == iLenText;
}

bool CFGAS_FormatString::ParseNull(const WideString& wsSrcText,
                                   const WideString& wsPattern) const {
  WideString wsTextFormat = GetTextFormat(wsPattern, L"null");

  int32_t iText = 0;
  size_t iPattern = 0;
  const wchar_t* pStrText = wsSrcText.c_str();
  int32_t iLenText = wsSrcText.GetLength();
  pdfium::span<const wchar_t> spTextFormat = wsTextFormat.AsSpan();
  while (iPattern < spTextFormat.size() && iText < iLenText) {
    if (spTextFormat[iPattern] == '\'') {
      WideString wsLiteral = GetLiteralText(spTextFormat, &iPattern);
      int32_t iLiteralLen = wsLiteral.GetLength();
      if (iText + iLiteralLen > iLenText ||
          wcsncmp(pStrText + iText, wsLiteral.c_str(), iLiteralLen)) {
        return false;
      }
      iText += iLiteralLen;
      iPattern++;
      continue;
    }
    if (spTextFormat[iPattern] != pStrText[iText])
      return false;

    iText++;
    iPattern++;
  }
  return iPattern == spTextFormat.size() && iText == iLenText;
}

bool CFGAS_FormatString::FormatText(const WideString& wsSrcText,
                                    const WideString& wsPattern,
                                    WideString* wsOutput) const {
  if (wsPattern.IsEmpty())
    return false;

  int32_t iLenText = wsSrcText.GetLength();
  if (iLenText == 0)
    return false;

  WideString wsTextFormat = GetTextFormat(wsPattern, L"text");

  int32_t iText = 0;
  size_t iPattern = 0;
  const wchar_t* pStrText = wsSrcText.c_str();
  pdfium::span<const wchar_t> spTextFormat = wsTextFormat.AsSpan();
  while (iPattern < spTextFormat.size()) {
    switch (spTextFormat[iPattern]) {
      case '\'': {
        *wsOutput += GetLiteralText(spTextFormat, &iPattern);
        iPattern++;
        break;
      }
      case 'A':
        if (iText >= iLenText || !FXSYS_iswalpha(pStrText[iText]))
          return false;

        *wsOutput += pStrText[iText++];
        iPattern++;
        break;
      case 'X':
        if (iText >= iLenText)
          return false;

        *wsOutput += pStrText[iText++];
        iPattern++;
        break;
      case 'O':
      case '0':
        if (iText >= iLenText || (!FXSYS_IsDecimalDigit(pStrText[iText]) &&
                                  !FXSYS_iswalpha(pStrText[iText]))) {
          return false;
        }
        *wsOutput += pStrText[iText++];
        iPattern++;
        break;
      case '9':
        if (iText >= iLenText || !FXSYS_IsDecimalDigit(pStrText[iText]))
          return false;

        *wsOutput += pStrText[iText++];
        iPattern++;
        break;
      default:
        *wsOutput += spTextFormat[iPattern++];
        break;
    }
  }
  return iText == iLenText;
}

bool CFGAS_FormatString::FormatStrNum(WideStringView wsInputNum,
                                      const WideString& wsPattern,
                                      WideString* wsOutput) const {
  ASSERT(!wsInputNum.IsEmpty());
  ASSERT(!wsPattern.IsEmpty());

  int32_t dot_index_f = -1;
  uint32_t dwNumStyle = 0;
  WideString wsNumFormat;
  LocaleIface* pLocale =
      GetNumericFormat(wsPattern, &dot_index_f, &dwNumStyle, &wsNumFormat);
  if (!pLocale || wsNumFormat.IsEmpty())
    return false;

  int32_t cc = 0;

  pdfium::span<const wchar_t> spNumFormat = wsNumFormat.AsSpan();
  WideString wsSrcNum(wsInputNum);
  wsSrcNum.TrimLeft('0');
  if (wsSrcNum.IsEmpty() || wsSrcNum[0] == '.')
    wsSrcNum.InsertAtFront('0');

  CFGAS_Decimal decimal = CFGAS_Decimal(wsSrcNum.AsStringView());
  if (dwNumStyle & FX_NUMSTYLE_Percent) {
    decimal = decimal * CFGAS_Decimal(100);
    wsSrcNum = decimal.ToWideString();
  }

  int32_t exponent = 0;
  if (dwNumStyle & FX_NUMSTYLE_Exponent) {
    int fixed_count = 0;
    if (dot_index_f > 0) {
      for (size_t ccf = 0; ccf < static_cast<size_t>(dot_index_f); ++ccf) {
        switch (spNumFormat[ccf]) {
          case '\'':
            GetLiteralText(spNumFormat, &ccf);
            break;
          case '9':
          case 'z':
          case 'Z':
            fixed_count++;
            break;
        }
      }
    }
    FX_SAFE_UINT32 threshold = 1;
    while (fixed_count > 1) {
      threshold *= 10;
      fixed_count--;
    }
    if (!threshold.IsValid())
      return false;

    bool bAdjusted = false;
    while (decimal.IsNotZero() &&
           fabs(decimal.ToDouble()) < threshold.ValueOrDie()) {
      decimal = decimal * CFGAS_Decimal(10);
      --exponent;
      bAdjusted = true;
    }
    if (!bAdjusted) {
      threshold *= 10;
      if (!threshold.IsValid())
        return false;

      while (decimal.IsNotZero() &&
             fabs(decimal.ToDouble()) > threshold.ValueOrDie()) {
        decimal = decimal / CFGAS_Decimal(10);
        ++exponent;
      }
    }
  }

  bool bTrimTailZeros = false;
  int32_t iTreading =
      GetNumTrailingLimit(wsNumFormat, dot_index_f, &bTrimTailZeros);
  int32_t scale = decimal.GetScale();
  if (iTreading < scale) {
    decimal.SetScale(iTreading);
    wsSrcNum = decimal.ToWideString();
  }
  if (bTrimTailZeros && scale > 0 && iTreading > 0) {
    wsSrcNum.TrimRight(L"0");
    wsSrcNum.TrimRight(L".");
  }

  WideString wsGroupSymbol = pLocale->GetGroupingSymbol();
  bool bNeg = false;
  if (wsSrcNum[0] == '-') {
    bNeg = true;
    wsSrcNum.Delete(0, 1);
  }

  bool bAddNeg = false;
  const wchar_t* str = wsSrcNum.c_str();
  int len = wsSrcNum.GetLength();
  auto dot_index = wsSrcNum.Find('.');
  if (!dot_index.has_value())
    dot_index = len;

  cc = dot_index.value() - 1;
  for (int ccf = dot_index_f - 1; ccf >= 0; --ccf) {
    switch (spNumFormat[ccf]) {
      case '9':
        if (cc >= 0) {
          if (!FXSYS_IsDecimalDigit(str[cc]))
            return false;

          wsOutput->InsertAtFront(str[cc]);
          cc--;
        } else {
          wsOutput->InsertAtFront(L'0');
        }
        break;
      case 'z':
        if (cc >= 0) {
          if (!FXSYS_IsDecimalDigit(str[cc]))
            return false;

          if (str[0] != '0')
            wsOutput->InsertAtFront(str[cc]);
          cc--;
        }
        break;
      case 'Z':
        if (cc >= 0) {
          if (!FXSYS_IsDecimalDigit(str[cc]))
            return false;

          wsOutput->InsertAtFront(str[0] == '0' ? L' ' : str[cc]);
          cc--;
        } else {
          wsOutput->InsertAtFront(L' ');
        }
        break;
      case 'S':
        if (bNeg) {
          *wsOutput = pLocale->GetMinusSymbol() + *wsOutput;
          bAddNeg = true;
        } else {
          wsOutput->InsertAtFront(L' ');
        }
        break;
      case 's':
        if (bNeg) {
          *wsOutput = pLocale->GetMinusSymbol() + *wsOutput;
          bAddNeg = true;
        }
        break;
      case 'E':
        *wsOutput = WideString::Format(L"E%+d", exponent) + *wsOutput;
        break;
      case '$':
        *wsOutput = pLocale->GetCurrencySymbol() + *wsOutput;
        break;
      case 'r':
        if (ccf - 1 >= 0 && spNumFormat[ccf - 1] == 'c') {
          if (bNeg)
            *wsOutput = L"CR" + *wsOutput;
          ccf--;
          bAddNeg = true;
        } else {
          wsOutput->InsertAtFront('r');
        }
        break;
      case 'R':
        if (ccf - 1 >= 0 && spNumFormat[ccf - 1] == 'C') {
          *wsOutput = bNeg ? L"CR" : L"  " + *wsOutput;
          ccf--;
          bAddNeg = true;
        } else {
          wsOutput->InsertAtFront('R');
        }
        break;
      case 'b':
        if (ccf - 1 >= 0 && spNumFormat[ccf - 1] == 'd') {
          if (bNeg)
            *wsOutput = L"db" + *wsOutput;
          ccf--;
          bAddNeg = true;
        } else {
          wsOutput->InsertAtFront('b');
        }
        break;
      case 'B':
        if (ccf - 1 >= 0 && spNumFormat[ccf - 1] == 'D') {
          *wsOutput = bNeg ? L"DB" : L"  " + *wsOutput;
          ccf--;
          bAddNeg = true;
        } else {
          wsOutput->InsertAtFront('B');
        }
        break;
      case '%':
        *wsOutput = pLocale->GetPercentSymbol() + *wsOutput;
        break;
      case ',':
        if (cc >= 0)
          *wsOutput = wsGroupSymbol + *wsOutput;
        break;
      case '(':
        wsOutput->InsertAtFront(bNeg ? L'(' : L' ');
        bAddNeg = true;
        break;
      case ')':
        wsOutput->InsertAtFront(bNeg ? L')' : L' ');
        break;
      case '\'':
        *wsOutput = GetLiteralTextReverse(spNumFormat, &ccf) + *wsOutput;
        break;
      default:
        wsOutput->InsertAtFront(spNumFormat[ccf]);
        break;
    }
  }

  if (cc >= 0) {
    int nPos = dot_index.value() % 3;
    wsOutput->clear();
    for (int32_t i = 0;
         i < pdfium::base::checked_cast<int32_t>(dot_index.value()); i++) {
      if (i % 3 == nPos && i != 0)
        *wsOutput += wsGroupSymbol;
      *wsOutput += wsSrcNum[i];
    }
    if (pdfium::base::checked_cast<int32_t>(dot_index.value()) < len) {
      *wsOutput += pLocale->GetDecimalSymbol();
      *wsOutput += wsSrcNum.Right(len - dot_index.value() - 1);
    }
    if (bNeg)
      *wsOutput = pLocale->GetMinusSymbol() + *wsOutput;
    return true;
  }
  if (dot_index_f ==
      pdfium::base::checked_cast<int32_t>(wsNumFormat.GetLength())) {
    if (!bAddNeg && bNeg)
      *wsOutput = pLocale->GetMinusSymbol() + *wsOutput;
    return true;
  }

  WideString wsDotSymbol = pLocale->GetDecimalSymbol();
  if (spNumFormat[dot_index_f] == 'V') {
    *wsOutput += wsDotSymbol;
  } else if (spNumFormat[dot_index_f] == '.') {
    if (pdfium::base::checked_cast<int32_t>(dot_index.value()) < len)
      *wsOutput += wsDotSymbol;
    else if (spNumFormat[dot_index_f + 1] == '9' ||
             spNumFormat[dot_index_f + 1] == 'Z')
      *wsOutput += wsDotSymbol;
  }

  cc = dot_index.value() + 1;
  for (size_t ccf = static_cast<size_t>(dot_index_f + 1);
       ccf < spNumFormat.size(); ++ccf) {
    switch (spNumFormat[ccf]) {
      case '\'':
        *wsOutput += GetLiteralText(spNumFormat, &ccf);
        break;
      case '9':
        if (cc < len) {
          if (!FXSYS_IsDecimalDigit(str[cc]))
            return false;

          *wsOutput += str[cc];
          cc++;
        } else {
          *wsOutput += L'0';
        }
        break;
      case 'z':
        if (cc < len) {
          if (!FXSYS_IsDecimalDigit(str[cc]))
            return false;

          *wsOutput += str[cc];
          cc++;
        }
        break;
      case 'Z':
        if (cc < len) {
          if (!FXSYS_IsDecimalDigit(str[cc]))
            return false;

          *wsOutput += str[cc];
          cc++;
        } else {
          *wsOutput += L'0';
        }
        break;
      case 'E': {
        *wsOutput += WideString::Format(L"E%+d", exponent);
        break;
      }
      case '$':
        *wsOutput += pLocale->GetCurrencySymbol();
        break;
      case 'c':
        if (ccf + 1 < spNumFormat.size() && spNumFormat[ccf + 1] == 'r') {
          if (bNeg)
            *wsOutput += L"CR";
          ccf++;
          bAddNeg = true;
        }
        break;
      case 'C':
        if (ccf + 1 < spNumFormat.size() && spNumFormat[ccf + 1] == 'R') {
          *wsOutput += bNeg ? L"CR" : L"  ";
          ccf++;
          bAddNeg = true;
        }
        break;
      case 'd':
        if (ccf + 1 < spNumFormat.size() && spNumFormat[ccf + 1] == 'b') {
          if (bNeg)
            *wsOutput += L"db";
          ccf++;
          bAddNeg = true;
        }
        break;
      case 'D':
        if (ccf + 1 < spNumFormat.size() && spNumFormat[ccf + 1] == 'B') {
          *wsOutput += bNeg ? L"DB" : L"  ";
          ccf++;
          bAddNeg = true;
        }
        break;
      case '%':
        *wsOutput += pLocale->GetPercentSymbol();
        break;
      case '8':
        while (ccf + 1 < spNumFormat.size() && spNumFormat[ccf + 1] == '8')
          ccf++;
        while (cc < len && FXSYS_IsDecimalDigit(str[cc])) {
          *wsOutput += str[cc];
          cc++;
        }
        break;
      case ',':
        *wsOutput += wsGroupSymbol;
        break;
      case '(':
        *wsOutput += bNeg ? '(' : ' ';
        bAddNeg = true;
        break;
      case ')':
        *wsOutput += bNeg ? ')' : ' ';
        break;
      default:
        break;
    }
  }
  if (!bAddNeg && bNeg) {
    *wsOutput = pLocale->GetMinusSymbol() + (*wsOutput)[0] +
                wsOutput->Right(wsOutput->GetLength() - 1);
  }
  return true;
}

bool CFGAS_FormatString::FormatNum(const WideString& wsSrcNum,
                                   const WideString& wsPattern,
                                   WideString* wsOutput) const {
  if (wsSrcNum.IsEmpty() || wsPattern.IsEmpty())
    return false;

  return FormatStrNum(wsSrcNum.AsStringView(), wsPattern, wsOutput);
}

bool CFGAS_FormatString::FormatDateTime(const WideString& wsSrcDateTime,
                                        const WideString& wsPattern,
                                        FX_DATETIMETYPE eDateTimeType,
                                        WideString* wsOutput) const {
  if (wsSrcDateTime.IsEmpty() || wsPattern.IsEmpty())
    return false;

  WideString wsDatePattern;
  WideString wsTimePattern;
  LocaleIface* pLocale = nullptr;
  FX_DATETIMETYPE eCategory =
      GetDateTimeFormat(wsPattern, &pLocale, &wsDatePattern, &wsTimePattern);
  if (!pLocale)
    return false;

  if (eCategory == FX_DATETIMETYPE_Unknown) {
    if (eDateTimeType == FX_DATETIMETYPE_Time)
      wsTimePattern = std::move(wsDatePattern);

    eCategory = eDateTimeType;
    if (eCategory == FX_DATETIMETYPE_Unknown)
      return false;
  }

  CFX_DateTime dt;
  auto iT = wsSrcDateTime.Find(L"T");
  if (!iT.has_value()) {
    if (eCategory == FX_DATETIMETYPE_Date &&
        FX_DateFromCanonical(wsSrcDateTime.AsSpan(), &dt)) {
      *wsOutput = FormatDateTimeInternal(dt, wsDatePattern, wsTimePattern, true,
                                         pLocale);
      return true;
    }
    if (eCategory == FX_DATETIMETYPE_Time &&
        FX_TimeFromCanonical(pLocale, wsSrcDateTime.AsSpan(), &dt)) {
      *wsOutput = FormatDateTimeInternal(dt, wsDatePattern, wsTimePattern, true,
                                         pLocale);
      return true;
    }
  } else {
    pdfium::span<const wchar_t> wsSrcDate =
        wsSrcDateTime.AsSpan().first(iT.value());
    pdfium::span<const wchar_t> wsSrcTime =
        wsSrcDateTime.AsSpan().subspan(iT.value() + 1);
    if (wsSrcDate.empty() || wsSrcTime.empty())
      return false;

    if (FX_DateFromCanonical(wsSrcDate, &dt) &&
        FX_TimeFromCanonical(pLocale, wsSrcTime, &dt)) {
      *wsOutput = FormatDateTimeInternal(dt, wsDatePattern, wsTimePattern,
                                         eCategory != FX_DATETIMETYPE_TimeDate,
                                         pLocale);
      return true;
    }
  }
  return false;
}

bool CFGAS_FormatString::FormatZero(const WideString& wsPattern,
                                    WideString* wsOutput) const {
  if (wsPattern.IsEmpty())
    return false;

  WideString wsTextFormat = GetTextFormat(wsPattern, L"zero");
  pdfium::span<const wchar_t> spTextFormat = wsTextFormat.AsSpan();
  for (size_t iPattern = 0; iPattern < spTextFormat.size(); ++iPattern) {
    if (spTextFormat[iPattern] == '\'') {
      *wsOutput += GetLiteralText(spTextFormat, &iPattern);
      continue;
    }
    *wsOutput += spTextFormat[iPattern];
  }
  return true;
}

bool CFGAS_FormatString::FormatNull(const WideString& wsPattern,
                                    WideString* wsOutput) const {
  if (wsPattern.IsEmpty())
    return false;

  WideString wsTextFormat = GetTextFormat(wsPattern, L"null");
  pdfium::span<const wchar_t> spTextFormat = wsTextFormat.AsSpan();
  for (size_t iPattern = 0; iPattern < spTextFormat.size(); ++iPattern) {
    if (spTextFormat[iPattern] == '\'') {
      *wsOutput += GetLiteralText(spTextFormat, &iPattern);
      continue;
    }
    *wsOutput += spTextFormat[iPattern];
  }
  return true;
}
