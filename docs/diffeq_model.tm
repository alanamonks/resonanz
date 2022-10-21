<TeXmacs|2.1>

<style|generic>

<\body>
  <\strong>
    Linear Model For Maximal Response (Approximation)
  </strong>

  Tomas Ukkonen, tomas.ukkonen@iki.fi

  \;

  Linear diff.eq. model.

  <math|\<b-e\><rprime|'><around*|(|t|)>=\<b-A\>*\<b-e\><around*|(|t|)>+\<Delta\><with|font-series|bold|s><around*|(|t,\<b-e\>|)>>,
  <math|\<b-s\><around*|(|t,\<b-e\>|)>> is difference caused by stimulation

  Need to solve matrix <math|\<b-A\>> from the system. This can be done from
  the measurements where we assume <math|\<b-Delta\>\<b-s\>> is small and MSE
  optimize for a linear problem, <math|\<b-Delta\>*\<b-E\>/\<Delta\>t=\<b-A\>*\<b-E\>>,
  where <math|\<b-Delta\>\<b-E\>> and <math|\<b-E\>> are measurements
  matrixes from the data. <math|\<b-A\>> is maybe time-dependant but change
  slowly (constant) so we keep estimating <math|\<b-A\><around*|(|t|)>> from
  measurements.

  The variable <math|\<b-e\><around*|(|t|)>> are brain EEG measurements or
  other variables computed and measured from the brain. We want to predict
  future <math|\<b-e\><around*|(|t<rsub|0>+T|)>> and minimize error

  <math|\<varepsilon\><around*|(|T|)>=<big|int><rsup|t<rsub|0>+T><rsub|t<rsub|0>><frac|1|2><around*|\<\|\|\>|\<b-e\><around*|(|t|)>-\<b-e\><rsub|target>|\<\|\|\>><rsup|2>*d*t>

  <math|<frac|d\<varepsilon\><around*|(|T|)>|d*T>=><math|<frac|1|2><around*|\<\|\|\>|\<b-e\><around*|(|t<rsub|0>+T|)>-\<b-e\><rsub|target>|\<\|\|\>><rsup|2>=0>

  \;

  Assuming <math|\<b-Delta\>\<b-s\>> is small after the initial step at
  <math|t<rsub|0>> we can solve for <math|\<b-e\><around*|(|t|)>>.

  <math|\<b-e\><around*|(|t|)>=exp<around*|(|\<b-A\>*<around*|(|t-t<rsub|0>|)>|)>*\<b-e\><around*|(|t<rsub|0>|)>>

  \;

  And we get equation

  <math|<frac|1|2><around*|\<\|\|\>|exp<around*|(|\<b-A\>*<around*|(|T-t<rsub|0>|)>|)>*\<b-e\><around*|(|t<rsub|0>|)>-\<b-e\><rsub|target>|\<\|\|\>><rsup|2>=0>

  This second order equation has easy solution when we solve for
  <math|\<b-e\><around*|(|t<rsub|0>|)>=\<b-e\><rsub|0>+\<b-Delta\>\<b-s\><around*|(|t<rsub|0>|)>>

  <math|*\<b-e\><rsub|0>+\<b-Delta\>\<b-s\><around*|(|t<rsub|0>|)>=exp<around*|(|\<b-A\>*<around*|(|T-t<rsub|0>|)>|)><rsup|-1>*\<b-e\><rsub|target>>

  <math|*\<b-Delta\>\<b-s\><around*|(|t<rsub|0>|)>=exp<around*|(|\<b-A\>*<around*|(|T-t<rsub|0>|)>|)><rsup|-1>*\<b-e\><rsub|target>-\<b-e\><rsub|0>>

  We assume starting point is always zero <math|t<rsub|0>=0> so the final
  target for stimulation is:

  <math|*\<b-Delta\>\<b-s\><around*|(|0|)>=exp<around*|(|\<b-A\>**T|)><rsup|-1>*\<b-e\><rsub|target>-\<b-e\><rsub|0>>

  \;

  \;

  After solving <math|\<Delta\>\<b-s\>>, <with|font-series|bold|approx.>
  optimal stimulation pictures are scanned for best stimulus that has minimal
  distance to target delta.

  \;

  The problem is that diff.eq. model is maybe WRONG. The difference is not
  maybe linearly dependent on current values of the points but only from the
  past difference (current slope of <math|\<b-e\>>).

  \;

  This means we can maybe extend measurements <math|\<b-e\><around*|(|t|)>>
  with extra slope variables\ 

  <math|<wide|\<b-e\>|^><around*|(|t|)>=<around*|[|\<b-e\><around*|(|t|)>,\<Delta\>e<around*|(|t|)>/\<Delta\>t|]>>.
  These slope variables are maybe linearly related to differential equation
  variables.

  \;
</body>

<\initial>
  <\collection>
    <associate|page-medium|paper>
  </collection>
</initial>