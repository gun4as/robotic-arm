#pragma once
// =============================================================================
//  Iestatījumu saglabāšana kontroliera atmiņā.
//  Mega: EEPROM.  LPC1769: pēdējais flash sektors (32 KB, 0x78000).
//  Saglabā: visu JointConfig (limiti, ātrumi, gala slēdži, HOME), TMC strāvas,
//  satvērēja impulsu robežas. Mikrosoļi un reduktors ir config.h (kompilējot).
// =============================================================================

namespace storage {
  bool load();      // false = nav derīgu datu (paliek noklusējumi)
  bool save();
  void factory();   // noklusējumi no config.h (atmiņu neaiztiek, kamēr nav SAVE)
}
