import sys
sys.path.append('../../common')
from env_indigo import *

indigo = Indigo()
def testSingleReactionSmarts (rxn1, rxn2, expected, hl):
  rxn1 = indigo.loadReactionSmarts(rxn1)
  rxn2 = indigo.loadReaction(rxn2)
  match = indigo.substructureMatcher(rxn2, "daylight-aam").match(rxn1)
  if match:
    print "matched",
  else:
    print "unmatched",
  if (match is None) == expected:
    print " (unexpected)",
  print
  if not match or not hl:
    return
  for mol in rxn1.iterateMolecules():
    print 'mol ', mol.index()
    for atom in mol.iterateAtoms():
      mapped = match.mapAtom(atom)
      if not mapped:
        mapped = '?'
      else:
        mapped = mapped.index()
      print 'atom ', atom.index(), '->', mapped
    for bond in mol.iterateBonds():
      mapped = match.mapBond(bond)
      if not mapped:
        mapped = '?'
      else:
        mapped = mapped.index()
      print 'bond ', bond.index(), '->', mapped
  print match.highlightedTarget().smiles()
  
testSingleReactionSmarts("C[H,O]>>C", "OC>>OC[H]", True, True)
testSingleReactionSmarts("C>>C", "CC>>CC", True, False)
testSingleReactionSmarts("C>>C", "[CH3:7][CH3:8]>>[CH3:7][CH3:8]", True, False)
testSingleReactionSmarts("[C:1]>>C", "[CH3:7][CH3:8]>>[CH3:7][CH3:8]", True, False)
testSingleReactionSmarts("[C:1]>>C", "[CH3:7][CH3:8]>>[CH3:7][CH3:8]", True, False)
testSingleReactionSmarts("[C:1]>>[C:1]", "[C]>>[C]", False, False)
testSingleReactionSmarts("[C:?1]>>[C:?1]", "[C]>>[C]", True, False)
testSingleReactionSmarts("[C:1]>>[C:1]", "[CH3:7][CH3:8]>>[CH3:7][CH3:8]", True, False)
testSingleReactionSmarts("[C:1]>>[C:2]", "[CH3:7][CH3:8]>>[CH3:7][CH3:8]", True, False)
testSingleReactionSmarts("[C:1][C:1]>>[C:1]", "[CH3:7][CH3:7]>>[CH3:7][CH3:7]", True, False)
testSingleReactionSmarts("[C:1][C:1]>>[C:1]", "[CH3:7][CH3:8]>>[CH3:7][CH3:8]", True, False)
testSingleReactionSmarts("[C:1][C:1]>>[C:1]", "[CH3:7][CH3:7]>>[CH3:7][CH3:8]", True, False)
testSingleReactionSmarts("[C$(CO)]>>[C$(CN)]", "OC>>NC", True, False)