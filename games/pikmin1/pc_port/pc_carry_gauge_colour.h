#ifndef PC_CARRY_GAUGE_COLOUR_H
#define PC_CARRY_GAUGE_COLOUR_H

/// An RGB triple for the carry counter digits (alpha comes from the gauge fade).
struct PcCarryGaugeRgb {
	int r, g, b;
};

/// Colours of the two numbers and the line between them over a carried object.
struct PcCarryGaugeColours {
	PcCarryGaugeRgb bottom; ///< Pikmin carrying now, and the dividing line.
	PcCarryGaugeRgb top;    ///< Pikmin needed to lift it.
};

/**
 * @brief Picks the carry counter colours (upstream issue #360).
 *
 * @param pikmin2Style The "Pikmin 2 Carry Colours" setting. Off is the
 *        GameCube look from the decomp (GaugeInfo::refresh): a blue target
 *        number over a red count and line, whoever is carrying.
 * @param carryColor PikiColor of the Onion the object is going to, or of most
 *        of its carriers before it sets off (carryGaugeColor in
 *        aiTransport.cpp). -1 when there is none, such as a box being pushed.
 *        Only read in Pikmin 2 style: both numbers and the line then take
 *        that colour, and -1 keeps the GameCube colours.
 */
inline PcCarryGaugeColours pc_carry_gauge_colours(bool pikmin2Style, int carryColor)
{
	// PikiColor values (GlobalGameOptions.h).
	const int kBlue = 0, kYellow = 2;
	const PcCarryGaugeRgb red = { 255, 32, 32 };
	const PcCarryGaugeRgb blue = { 32, 32, 255 };
	const PcCarryGaugeRgb yellow = { 255, 255, 32 };

	PcCarryGaugeColours colours = { red, blue };
	if (!pikmin2Style || carryColor < 0) return colours;

	const PcCarryGaugeRgb carrier = carryColor == kBlue ? blue : carryColor == kYellow ? yellow : red;
	colours.bottom = carrier;
	colours.top = carrier;
	return colours;
}

#endif // PC_CARRY_GAUGE_COLOUR_H
