<?xml version="1.0"?>
<xsl:stylesheet
    version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:xhtml="http://www.w3.org/1999/xhtml"
    xmlns="http://www.w3.org/1999/xhtml">

<xsl:output indent="yes" />

<xsl:template match="eas">
  <xsl:variable name="station" select="station" />
  <xsl:variable name="ustation" select="translate($station, 'abcdefghijklmnopqrstuvwxyz', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ')" />
  <html xml:lang="en" lang="en">
    <head>
      <title><xsl:value-of select="$ustation" />: NOAA Weather Radio</title>
      <link href="/style.css" rel="stylesheet" type="text/css" />
    </head>
    <body>
      <h1><xsl:value-of select="$ustation" />: NOAA Weather Radio</h1>
      <table border="1">
        <tr>
          <th>Originator</th>
          <th>Event</th>
          <th>Issued</th>
          <th>Audio</th>
          <th>Counties</th>
        </tr>
        <xsl:for-each select="message">
          <tr>
            <td><xsl:value-of select="originator" /></td>
            <td><xsl:value-of select="event" /></td>
            <td><xsl:value-of select="issued" /></td>
            <td><a href="{$station}/{filename}">Listen</a></td>
            <td>
              <xsl:for-each select="area">
                <xsl:value-of select="@county" />
                <xsl:if test="position() != last()">, </xsl:if>
              </xsl:for-each>
            </td>
          </tr>
        </xsl:for-each>
      </table>
    </body>
  </html>
</xsl:template>

</xsl:stylesheet>
